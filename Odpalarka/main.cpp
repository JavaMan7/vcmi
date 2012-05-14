//#include "../global.h"
#include "StdInc.h"
#include "../lib/VCMI_Lib.h"
namespace po = boost::program_options;


//FANN
#include <doublefann.h>
#include <fann_cpp.h>

std::string leftAI, rightAI, battle, results, logsDir;
bool withVisualization = false;
std::string servername;
std::string runnername;
extern DLL_EXPORT LibClasses * VLC;

std::string addQuotesIfNeeded(const std::string &s)
{
	if(s.find_first_of(' ') != std::string::npos)
		return "\"" + s + "\"";

	return s;
}

void prog_help() 
{
	std::cout << "If run without args, then StupidAI will be run on b1.json.\n";
}

void runCommand(const std::string &command, const std::string &name, const std::string &logsDir = "")
{
	static std::string commands[100];
	static int i = 0;
	std::string &cmd = commands[i++];
	if(logsDir.size() && name.size())
	{
		std::string directionLogs = logsDir + "/" + name + ".txt";
		cmd = command + " > " + addQuotesIfNeeded(directionLogs);
	}
	else
		cmd = command;

	boost::thread tt(boost::bind(std::system, cmd.c_str()));
}

double playBattle(const DuelParameters &dp)
{
	{
		CSaveFile out("pliczek.ssnb");
		out << dp;
	}


	std::string serverCommand = servername + " " + addQuotesIfNeeded(battle) + " " + addQuotesIfNeeded(leftAI) + " " + addQuotesIfNeeded(rightAI) + " " + addQuotesIfNeeded(results) + " " + addQuotesIfNeeded(logsDir) + " " + (withVisualization ? " v" : "");
	std::string runnerCommand = runnername + " " + addQuotesIfNeeded(logsDir);
	std::cout <<"Server command: " << serverCommand << std::endl << "Runner command: " << runnerCommand << std::endl;

	int code = 0;
	boost::thread t([&]
	{ 
		code = std::system(serverCommand.c_str());
	});

	runCommand(runnerCommand, "first_runner", logsDir);
	runCommand(runnerCommand, "second_runner", logsDir);
	runCommand(runnerCommand, "third_runner", logsDir);
	if(withVisualization)
	{
		//boost::this_thread::sleep(boost::posix_time::millisec(500)); //FIXME
		boost::thread tttt(boost::bind(std::system, "VCMI_Client.exe -battle"));
	}

	//boost::this_thread::sleep(boost::posix_time::seconds(5));
	t.join();
	return code / 1000000.0;
}

typedef std::map<int, CArtifactInstance*> TArtSet;


double cmpArtSets(DuelParameters dp, TArtSet setL, TArtSet setR)
{
	//lewa strona z art 0.9
	//bez artefaktow -0.41
	//prawa strona z art. -0.926

	dp.sides[0].artifacts = setL;
	dp.sides[1].artifacts = setR;

	auto battleOutcome = playBattle(dp);
	return battleOutcome;
}

std::vector<CArtifactInstance*> genArts()
{
	std::vector<CArtifactInstance*> ret;

	CArtifact *nowy = new CArtifact();
	nowy->description = "Cudowny miecz Towa gwarantuje zwyciestwo";
	nowy->name = "Cudowny miecz";
	nowy->constituentOf = nowy->constituents = NULL;
	nowy->possibleSlots.push_back(Arts::LEFT_HAND);

	CArtifactInstance *artinst = new CArtifactInstance(nowy);
	auto &arts = VLC->arth->artifacts;
	CArtifactInstance *inny = new CArtifactInstance(VLC->arth->artifacts[15]);

	artinst->addNewBonus(new Bonus(Bonus::PERMANENT, Bonus::PRIMARY_SKILL, Bonus::ARTIFACT_INSTANCE, +25, nowy->id, PrimarySkill::ATTACK));
	artinst->addNewBonus(new Bonus(Bonus::PERMANENT, Bonus::PRIMARY_SKILL, Bonus::ARTIFACT_INSTANCE, +25, nowy->id, PrimarySkill::DEFENSE));

	auto bonuses = artinst->getBonuses([](const Bonus *){ return true; });
	BOOST_FOREACH(Bonus *b, *bonuses) 
	{
		std::cout << format("%s (%d) value:%d, description: %s\n") % bonusTypeToString(b->type) % b->subtype % b->val % b->Description();
	}

	return ret;
}

//returns how good the artifact is for the neural network
double runSSN(FANN::neural_net & net, CArtifactInstance * inst)
{

	return 0.0;
}


const unsigned int num_input = 2;

double * genSSNinput(const DuelParameters & dp, CArtifactInstance * art)
{
	double * ret = new double[num_input];
	double * cur = ret;

	//general description

	*(cur++) = dp.bfieldType;
	*(cur++) = dp.terType;

	//creature & hero description

	for(int i=0; i<2; ++i)
	{
		auto & side = dp.sides[0];
		*(cur++) = side.heroId;
		for(int k=0; k<4; ++k)
			*(cur++) = side.heroPrimSkills[k];
		for(int i=0; i<7; ++i)
		{
			*(cur++) = side.stacks[i].type;
			*(cur++) = side.stacks[i].count;
		}
	}

	//bonus description

	return ret;
}

void learnSSN(FANN::neural_net & net, const DuelParameters & dp, CArtifactInstance * art, double desiredVal)
{
	double * input = genSSNinput(dp, art);
	net.train(input, &desiredVal);
	delete input;
}

void initNet(FANN::neural_net & ret)
{
	const float learning_rate = 0.7f;
	const unsigned int num_layers = 3;
	const unsigned int num_hidden = 3;
	const unsigned int num_output = 1;
	const float desired_error = 0.001f;
	const unsigned int max_iterations = 300000;
	const unsigned int iterations_between_reports = 1000;

	ret.create_standard(num_layers, num_input, num_hidden, num_output);

	ret.set_learning_rate(learning_rate);

	ret.set_activation_steepness_hidden(1.0);
	ret.set_activation_steepness_output(1.0);

	ret.set_activation_function_hidden(FANN::SIGMOID_SYMMETRIC_STEPWISE);
	ret.set_activation_function_output(FANN::SIGMOID_SYMMETRIC_STEPWISE);

	ret.randomize_weights(0.0, 1.0);
}

void SSNRun()
{
	auto availableArts = genArts();
	std::vector<std::pair<CArtifactInstance *, double> > artNotes;

	TArtSet setL, setR;

	FANN::neural_net network;
	initNet(network);

	for(int i=0; i<availableArts.size(); ++i)
	{
		artNotes.push_back(std::make_pair(availableArts[i], runSSN(network, availableArts[i])));
	}
	boost::range::sort(artNotes,
		[](const std::pair<CArtifactInstance *, double> & a1, const std::pair<CArtifactInstance *, double> & a2)
		{return a1.second > a2.second;});

	//pick best arts into setL
	BOOST_FOREACH(auto & ap, artNotes)
	{
		auto art = ap.first;
		BOOST_FOREACH(auto slot, art->artType->possibleSlots)
		{
			if(setL.find(slot) != setL.end())
			{
				setL[slot] = art;
				break;
			}
		}
	}


	//duels to test on
	std::vector<DuelParameters> dps;
	for(int k = 0; k<10; ++k)
	{
		DuelParameters dp;
		dp.bfieldType = 1;
		dp.terType = 1;

		auto &side = dp.sides[0];
		side.heroId = 0;
		side.heroPrimSkills.resize(4,0);
		side.stacks[0] = DuelParameters::SideSettings::StackSettings(10+k*3, rand()%30);
		dp.sides[1] = side;
		dp.sides[1].heroId = 1;
	}

	//evaluate
	for(int i=0; i<dps.size(); ++i)
	{
		auto & dp = dps[i];
		double resultLR = cmpArtSets(dp, setL, setR),
			resultRL = cmpArtSets(dp, setR, setL),
			resultsBase = cmpArtSets(dp, TArtSet(), TArtSet());

		double LRgain = resultLR - resultsBase,
			RLgain = resultRL - resultsBase;
	}
}

int main(int argc, char **argv)
{
	std::cout << "VCMI Odpalarka\nMy path: " << argv[0] << std::endl;

	po::options_description opts("Allowed options");
	opts.add_options()
		("help,h", "Display help and exit")
		("aiLeft,l", po::value<std::string>()->default_value("StupidAI"), "Left AI path")
		("aiRight,r", po::value<std::string>()->default_value("StupidAI"), "Right AI path")
		("battle,b", po::value<std::string>()->default_value("pliczek.ssnb"), "Duel file path")
		("resultsOut,o", po::value<std::string>()->default_value("./results.txt"), "Output file when results will be appended")
		("logsDir,d", po::value<std::string>()->default_value("."), "Directory where log files will be created")
		("visualization,v", "Runs a client to display a visualization of battle");


	try
	{
		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, opts), vm);
		po::notify(vm);

		if(vm.count("help"))
		{
			opts.print(std::cout);
			prog_help();
			return 0;
		}

		leftAI = vm["aiLeft"].as<std::string>();
		rightAI = vm["aiRight"].as<std::string>();
		battle = vm["battle"].as<std::string>();
		results = vm["resultsOut"].as<std::string>();
		logsDir = vm["logsDir"].as<std::string>();
		withVisualization = vm.count("visualization");
	}
	catch(std::exception &e) 
	{
		std::cerr << "Failure during parsing command-line options:\n" << e.what() << std::endl;
		exit(1);
	}

	std::cout << "Config:\n" << leftAI << " vs " << rightAI << " on " << battle << std::endl;

	if(leftAI.empty() || rightAI.empty() || battle.empty())
	{
		std::cerr << "I wasn't able to retreive names of AI or battles. Ending.\n";
		return 1;
	}



	runnername = 
#ifdef _WIN32
		"VCMI_BattleAiHost.exe"
#else
		"./vcmirunner"
#endif
	;
	servername = 
#ifdef _WIN32
		"VCMI_server.exe"
#else
		"./vcmiserver"
#endif
	;

	
	VLC = new LibClasses();
	VLC->init();

	SSNRun();

	return EXIT_SUCCESS;
}