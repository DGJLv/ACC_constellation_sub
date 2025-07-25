#include "ACC.h" 
#include <cstdlib> 
#include <bitset> 
#include <sstream> 
#include <string> 
#include <thread> 
#include <algorithm> 
#include <fstream> 
#include <atomic> 
#include <signal.h> 
#include <unistd.h> 
#include <cstring>
#include <chrono> 
#include <iomanip>
#include <numeric>
#include <ctime>

using namespace std;


/*ID:3+4 sigint handling*/
std::atomic<bool> quitacc(false); 
void ACC::got_signal(int){quitacc.store(true);}

/*------------------------------------------------------------------------------------*/
/*--------------------------------Constructor/Deconstructor---------------------------*/

/*ID:5 Constructor*/
ACC::ACC() : eth_("192.168.46.107", "2007"), eth_burst_("192.168.46.107", "2008"), running_(true)
{
}

ACC::ACC(const std::string& ip) : eth_(ip, "2007"), eth_burst_(ip, "2008")
{
}

/*ID:6 Destructor*/
ACC::~ACC()
{
}

ACC::ConfigParams::ConfigParams() :
    ip(""),
    rawMode(true),
    eventNumber(100),
    triggerMode(1),
    boardMask(0xff),
    label("testData"),
    reset(false),
    accTrigPolarity(0),
    validationStart(0),
    validationWindow(0),
    coincidentTrigMask(0x0f)
{
    for(int i = 0; i < 8; ++i)
    {
        coincidentTrigDelay[i] = 0;
        coincidentTrigStretch[i] = 5;
    }
}

/*------------------------------------------------------------------------------------*/
/*---------------------------Setup functions for ACC/ACDC-----------------------------*/
/** 
void ACC::parseConfig(const YAML::Node& config)
{
    if(config["humanReadableData"]) params_.rawMode = !config["humanReadableData"].as<bool>();

    params_.eventNumber = config["nevents"].as<int>();
    params_.triggerMode = config["triggerMode"].as<int>();

    params_.boardMask = config["ACDCMask"].as<unsigned int>();
	
    if(config["fileLabel"]) params_.label = config["fileLabel"].as<std::string>();

    if(config["resetACCOnStart"]) params_.reset = config["resetACCOnStart"].as<bool>();

    if(config["accTrigPolarity"]) params_.accTrigPolarity = config["accTrigPolarity"].as<int>();

    if(config["validationStart"])  params_.validationStart = config["validationStart"].as<int>();
    if(config["validationWindow"]) params_.validationWindow = config["validationWindow"].as<int>();

    if(config["coincidentTrigMask"]) params_.coincidentTrigMask = config["coincidentTrigMask"].as<int>();

    for(int i = 0; i < 8; ++i)
    {
        if(config["coincidentTrigDelay_"   + std::to_string(i)]) params_.coincidentTrigDelay[i]   = config["coincidentTrigDelay_"   + std::to_string(i)].as<int>();
        if(config["coincidentTrigStretch_" + std::to_string(i)]) params_.coincidentTrigStretch[i] = config["coincidentTrigStretch_" + std::to_string(i)].as<int>();
    }
}
*/

  void ACC::parseConfig(const constellation::config::Configuration& config)
{
    if(config.has("humanReadableData")) params_.rawMode = !config.get<bool>("humanReadableData");

    params_.eventNumber = config.get<int>("nevents");
    params_.triggerMode = config.get<int>("triggerMode");

    params_.boardMask = config.get<unsigned int>("ACDCMask");
    if (config.has("ip")) params_.ip = config.get<std::string>("ip");
    if(config.has("fileLabel")) params_.label = config.get<std::string>("fileLabel");

    if(config.has("resetACCOnStart")) params_.reset = config.get<bool>("resetACCOnStart");

    if(config.has("accTrigPolarity")) params_.accTrigPolarity = config.get<int>("accTrigPolarity");

    if(config.has("validationStart"))  params_.validationStart = config.get<int>("validationStart");
    if(config.has("validationWindow")) params_.validationWindow = config.get<int>("validationWindow");

    if(config.has("coincidentTrigMask")) params_.coincidentTrigMask = config.get<int>("coincidentTrigMask");

    cout << "configured"; 

    for(int i = 0; i < 8; ++i)
    {
        if(config.has("coincidentTrigDelay_"   + std::to_string(i))) params_.coincidentTrigDelay[i]   = config.get<int>("coincidentTrigDelay_"   + std::to_string(i));
        if(config.has("coincidentTrigStretch_" + std::to_string(i))) params_.coincidentTrigStretch[i] = config.get<int>("coincidentTrigStretch_" + std::to_string(i));
    }

}


/*ID:9 Create ACDC class instances for each connected ACDC board*/
int ACC::createAcdcs()
{
	//Check for connected ACDC boards
	std::vector<int> connectedBoards = whichAcdcsConnected(); 
	if(connectedBoards.size() == 0)
	{
		std::cout << "Trying to reset ACDC boards" << std::endl;
		eth_.send(0x100, 0xFFFF00);
		usleep(10000);
		connectedBoards = whichAcdcsConnected();
		if(connectedBoards.size() == 0)
		{
			std::cout << "After ACDC reset no changes, still no boards found" << std::endl;
		}
	}

	//if there are no ACDCs, return 0
	if(connectedBoards.size() == 0)
	{
		writeErrorLog("No aligned ACDC indices");
		return 0;
	}
	
	//Clear the ACDC class map if one exists
	acdcs_.clear();

	//Create ACDC objects with their board numbers
	for(int bi: connectedBoards)
	{
		acdcs_.emplace_back(bi);
	}

	if(acdcs_.size()==0)
	{
		writeErrorLog("No ACDCs created even though there were boards found!");
		return 0;
	}

	return 1;
}

/*ID:11 Queries the ACC for information about connected ACDC boards*/
std::vector<int> ACC::whichAcdcsConnected()
{
	unsigned int command;
	vector<int> connectedBoards;

	//New sequence to ask the ACC to reply with the number of boards connected 
	//Disables the PSEC4 frame data transfer for this sequence. Has to be set to HIGH later again
	enableTransfer(0); 
	usleep(1000);

	//Resets the RX buffer on all 8 ACDC boards
	eth_.send(0x0020, 0xff);

	//Request and read the ACC info buffer and pass it the the corresponding vector
        uint64_t accInfo = eth_.recieve(0x1011);

	unsigned short alignment_packet = ~((unsigned short)accInfo);
	for(int i = 0; i < MAX_NUM_BOARDS; i++)
	{	
		//both (1<<i) and (1<<i+8) should be true if aligned & synced respectively
		if((alignment_packet & (1 << i)))
		{
			//the i'th board is connected
			connectedBoards.push_back(i);
		}
	}

	cout << "Connected Boards: " << connectedBoards.size() << endl;
	return connectedBoards;
	cout << "board connected";
}

int ACC::initializeConfig(const constellation::config::Configuration& config)
{
    int retval;
    parseConfig(config);

    if(params_.reset) 
    {
        resetACC();
        usleep(5000);
    }

	// Creates ACDCs for readout
	retval = createAcdcs();
	if(retval==0)
	{
        writeErrorLog("ACDCs could not be created");
	}


    for(ACDC& acdc : acdcs_)
	{
        //parse general ACDC settings
        acdc.parseConfig(config);

        //parse ACDC specific settings
        if(config.has("ACDC" + std::to_string(acdc.getBoardIndex())))
        {
            acdc.parseConfig(config.get<constellation::config::Dictionary>("ACDC" + std::to_string(acdc.getBoardIndex())));
        }

    }
    return 0;
}

/*ID 17: Main init function that controls generalk setup as well as trigger settings*/
// int ACC::initializeForDataReadout(const YAML::Node& config, const string& timestamp)
int ACC::initializeForDataReadout(const string& timestamp)
{
	unsigned int command;
	int retval;
    
    string datafn;

    //read ACC parameters from cfg file
    // launch 
    // parseConfig(config);

    //parse ACC specific settings
    // if(config.has("ACC0"))
    // {
    //     parseConfig(config.get<constellation::config::Dictionary>("ACC0"));
    // }

    // if(params_.reset) 
    // {
    //     resetACC();
    //     usleep(5000);
    // }

	// // Creates ACDCs for readout
	// retval = createAcdcs();
	// if(retval==0)
	// {
    //     writeErrorLog("ACDCs could not be created");
	// }

    //clear slow RX buffers just in case they have leftover data.
    eth_.send(0x00000002, 0xff);

    //check ACDC PLL Settings
    // REVIEW ERRORS AND MESSAGES

    // TODO: separate config
    // config file should be 
    // paseconfig, create ACDCs, parseconfig acdc should be in initialization
    // other can be in launch

    unsigned int boardsForRead = 0;
    for(ACDC& acdc : acdcs_)
	{
        // //parse general ACDC settings
        // acdc.parseConfig(config);

        // //parse ACDC specific settings
        // if(config.has("ACDC" + std::to_string(acdc.getBoardIndex())))
        // {
        //     acdc.parseConfig(config.get<constellation::config::Dictionary>("ACDC" + std::to_string(acdc.getBoardIndex())));
        // }

        //reset if requested
        if(acdc.params_.reset) resetACDC(1 << acdc.getBoardIndex());

        // read ACDC info frame 
        eth_.send(0x100, 0x00D00000 | (1 << (acdc.getBoardIndex() + 24)));
	cout << "Readout ACDC info frame";

        //wait until we have fully received all 32 expected words from the ACDC
        int iTimeout = 10;
        while(eth_.recieve(0x1138+acdc.getBoardIndex()) < 32 && iTimeout > 0)
        {
            usleep(10);
            ++iTimeout;
        }
        if(iTimeout == 0) 
        {
            std::cout << "ERROR: ACDC info frame retrieval timeout" << std::endl;
            exit(1);
        }

        std::vector<uint64_t> acdcInfo = eth_.recieve_many(0x1200 + acdc.getBoardIndex(), 32, EthernetInterface::NO_ADDR_INC);
        if((acdcInfo[0] & 0xffff) != 0x1234)
        {
            std::cout << "ACDC" << acdc.getBoardIndex() << " has invalid info frame" << std::endl;
        }

        if(!(acdcInfo[6] & 0x4)) std::cout << "ACDC" << acdc.getBoardIndex() << " has unlocked ACC pll" << std::endl;
        if(!(acdcInfo[6] & 0x2)) std::cout << "ACDC" << acdc.getBoardIndex() << " has unlocked serial pll" << std::endl;
        if(!(acdcInfo[6] & 0x1)) std::cout << "ACDC" << acdc.getBoardIndex() << " has unlocked white rabbit pll" << std::endl;

        //check JCPLL lock signal 
        if(!(acdcInfo[6] & 0x200))
        {
            // external PLL must be unconfigured, attempt to configure them 
            configJCPLL();

            // reset the ACDC after configuring JCPLL
            resetACDC();
            usleep(500000);

            // check PLL bit again
            // read ACD info frame 
            eth_.send(0x100, 0x00D00000 | (1 << (acdc.getBoardIndex() + 24)));

            acdcInfo = eth_.recieve_many(0x1200 + acdc.getBoardIndex(), 32, EthernetInterface::NO_ADDR_INC);
            if((acdcInfo[0] & 0xffff) != 0x1234)
            {
                std::cout << "ACDC" << acdc.getBoardIndex() << " has invalid info frame" << std::endl;
            }

            if(!(acdcInfo[6] & 0x200)) writeErrorLog("ACDC" + std::to_string(acdc.getBoardIndex()) + " has unlocked JC pll");
                
        }

        if(!(acdcInfo[6] & 0x8)) writeErrorLog("ACDC" + std::to_string(acdc.getBoardIndex()) + " has unlocked sys pll");


        //set pedestal settings
        if(acdc.params_.pedestals.size() == 5)
        {
            setPedestals(1 << acdc.getBoardIndex(), acdc.params_.pedestals);
        }

        //set dll_vdd
        for(int iPSEC = 0; iPSEC < 5; ++iPSEC)
        {
            eth_.send(0x100, 0x00A00000 | (1 << (acdc.getBoardIndex() + 24)) | (iPSEC << 12) | acdc.params_.dll_vdd);
        }

        eth_.send(0x100, 0x00B70000 | (1 << (acdc.getBoardIndex() + 24)) | (acdc.params_.acc_backpressure?1:0));

        //reset dll
        //eth_.send(0x100, 0x00F20000 | (1 << (acdc.getBoardIndex() + 24)));
            
        //if((1 << acdc.getBoardIndex()) & params_.boardMask) ++boardsForRead;
    }


    //usleep(100000);

	//disable all triggers
	//ACC trigger
    for(unsigned int i = 0; i < 8; ++i) eth_.send(0x0030+i, 0);
	//ACDC trigger
	command = 0xffB00000;
	eth_.send(0x100, command);
	cout << "trigger disabled";
    //disable data transmission
    enableTransfer(0);
    //disable "auto-transmit" mode for ACC data readout 
    eth_.send(0x23, 0);


    //flush data FIFOs
	dumpData(params_.boardMask);

	//train manchester links
	eth_.send(0x0060, 0);
	usleep(250);
    // unsigned int boardsForRead = 0;
    for(ACDC& acdc : acdcs_)
	{
        // //parse general ACDC settings
        // acdc.parseConfig(config);

        // // //parse ACDC specific settings
        // if(config.has("ACDC" + std::to_string(acdc.getBoardIndex())))
        // {
        //     acdc.parseConfig(config.get<constellation::config::Dictionary>("ACDC" + std::to_string(acdc.getBoardIndex())));
        // }
    //scan hs link phases and pick optimal phase
    scanLinkPhase(0xff);

    // Toogels the calibration mode on if requested
	for(ACDC& acdc : acdcs_) 
    {
        unsigned int acdcMask = 1 << acdc.getBoardIndex();
        if(acdcMask & params_.boardMask) toggleCal(acdc.params_.calibMode, 0x7FFF, acdcMask);
    }

	// Set trigger conditions
	switch(params_.triggerMode)
	{ 	
    case 0: //OFF
        writeErrorLog("Trigger source turned off");	
        break;
    case 1: //Software trigger
        setHardwareTrigSrc(params_.triggerMode,params_.boardMask);
        break;
    case 2: //Self trigger
			//setHardwareTrigSrc(params_.triggerMode,params_.boardMask);
        goto selfsetup;
    case 5: //Self trigger with SMA validation on ACC
        eth_.send(0x0038, params_.accTrigPolarity);

        eth_.send(0x0039, params_.validationStart);
			
        eth_.send(0x003a, params_.validationWindow);
    case 4: // ACC coincident 
        eth_.send(0x003f, params_.coincidentTrigMask);

        for(int i = 0; i < 8; ++i)
        {
            eth_.send(0x0040+i, params_.coincidentTrigDelay[i]);
            eth_.send(0x0048+i, params_.coincidentTrigStretch[i]);
        }
                        
    case 3: //Self trigger with validation 
        setHardwareTrigSrc(params_.triggerMode,params_.boardMask);
        //timeout after 1 us 
        command = 0x00B20000;
        command = (command | (params_.boardMask << 24)) | 40;
        eth_.send(0x100, command);
        goto selfsetup;
    default: // ERROR case
        writeErrorLog("Trigger mode unrecognizes Error");	
        break;
    selfsetup:
        command = 0x00B10000;
			
        for(ACDC& acdc : acdcs_)
        {
            //skip ACDC if it is not included in boardMask
            unsigned int acdcMask = 1 << acdc.getBoardIndex();
            if(!(params_.boardMask & acdcMask)) continue;

            if(acdc.params_.selfTrigMask.size() != 5)
            {
                writeErrorLog("PSEC trigger mask error");	
            }
			
            std::vector<unsigned int> CHIPMASK = {0x00000000,0x00001000,0x00002000,0x00003000,0x00004000};
            for(int i=0; i<(int)acdc.params_.selfTrigMask.size(); i++)
            {		
				command = 0x00B10000;
				command = (command | (acdcMask << 24)) | CHIPMASK[i] | acdc.params_.selfTrigMask[i]; 
                //printf("%x\n", command);
				eth_.send(0x100, command);
            }
			
            command = 0x00B16000;
            command = (command | (acdcMask << 24)) | acdc.params_.selfTrigPolarity;
            eth_.send(0x100, command);
//                            command = 0x00B15000;
//                            command = (command | (acdcMask << 24)) | SELF_number_channel_coincidence;
//                            eth_.send(0x100, command);
//                            command = 0x00B18000;
//                            command = (command | (acdcMask << 24)) | SELF_coincidence_onoff;
//                            eth_.send(0x100, command);

            if(acdc.params_.triggerThresholds.size() == 30)
            {
                for(int iChip = 0; iChip < 5; ++iChip)
                {
                    for(int iChan = 0; iChan < 6; ++iChan)
                    {
                        command = 0x00A60000;
                        command = ((command + (iChan << 16)) | (acdcMask << 24)) | (iChip << 12) | acdc.params_.triggerThresholds[6*iChip + iChan];
                        eth_.send(0x100, command);
                    }
                }
            }
            else
            {
                writeErrorLog("Incorrect number of trigger thresholds: " + std::to_string(acdc.params_.triggerThresholds.size()));
            }
        }
	}

    //set fifo backpressure depth to maximum
    eth_.send(0x57, 0xe1);

    auto t0 = std::chrono::high_resolution_clock::now();

    //flush data FIFOs
	dumpData(params_.boardMask);
    //eth_.send(0x1ffffffff, 0x1);
    
    // TODO: move functionality to receiver unit
    //create file objects
    // string rawfn;
    // if(params_.rawMode==true)
    // {
    //     rawfn = outfilename + "Raw_";
    //     if(params_.label.size() > 0) rawfn += params_.label + "_";
    //     rawfn += timestamp + "_b";
    // }

    // for(ACDC& acdc: acdcs_)
    // {
    //     //base command for set data readmode and which board bi to read
    //     int bi = acdc.getBoardIndex();

    //     //skip if board is not active 
    //     if(!((1 << bi) & params_.boardMask)) continue;

    //     acdc.createFile(rawfn);
    // }

    //some setup needs at least 100 ms to complete 
    auto t1 = std::chrono::high_resolution_clock::now();
    while(std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count() < 200000000)
    {
        usleep(1000);
        t1 = std::chrono::high_resolution_clock::now();
    }


    
}return 0;
}


    void ACC::initializeFile(const string& timestamp)
    {
    auto t0 = std::chrono::high_resolution_clock::now();
    string outfilename = "./Results/";
    string rawfn;
    if(params_.rawMode==true)
    {
        rawfn = outfilename + "Raw_";
        if(params_.label.size() > 0) rawfn += params_.label + "_";
        rawfn += timestamp + "_b";
    }

    for(ACDC& acdc: acdcs_)
    {
        //base command for set data readmode and which board bi to read
        int bi = acdc.getBoardIndex();

        //skip if board is not active 
        if(!((1 << bi) & params_.boardMask)) continue;

        acdc.createFile(rawfn);
    }

    //some setup needs at least 100 ms to complete 
    auto t1 = std::chrono::high_resolution_clock::now();
    while(std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count() < 200000000)
    {
        usleep(1000);
        t1 = std::chrono::high_resolution_clock::now();
    }


    }

void ACC::startRun()
{
	//Enables the transfer of data from ACDC to ACC
	eth_burst_.setBurstTarget();
	eth_.setBurstMode(true);
	enableTransfer(3); 

    //launch file writer thread
    //protect variable with mutex?!?


    //enable "auto-transmit" mode for ACC data readout 
    eth_.send(0x23, 1);

    setHardwareTrigSrc(params_.triggerMode,params_.boardMask);
}

// void ACC::startRun_R()
// {
//     runWriteThread_ = true;
//     data_write_thread_.reset(new std::thread(&ACC::writeThread, this));
// }


void ACC::stopRun()
{
    runWriteThread_ = false;
}

/*ID 21: Set up the hardware trigger*/
void ACC::setHardwareTrigSrc(int src, unsigned int boardMask)
{
	if(src > 9){
		string err_msg = "Source: ";
		err_msg += to_string(src);
		err_msg += " will cause an error for setting Hardware triggers. Source has to be <9";
		writeErrorLog(err_msg);
	}

        int ACCtrigMode = 0;
        int ACDCtrigMode = 0;
        switch(src)
        {
        case 0:
            ACCtrigMode = 0;
            ACDCtrigMode = 0;
            break;
        case 1:
            ACCtrigMode = 1;
            ACDCtrigMode = 1;
            break;
        case 2:
            ACCtrigMode = 0;
            ACDCtrigMode = 2;
            break;
        case 3:
            ACCtrigMode = 2;
            ACDCtrigMode = 1;
            break;
        case 4:
            ACCtrigMode = 5;
            ACDCtrigMode = 3;
            break;
        default:
            ACCtrigMode = 0;
            ACDCtrigMode = 0;
            break;
        
        }

	//ACC hardware trigger
        for(unsigned int i = 0; i < 8; ++i)
        {
            if((boardMask >> i) & 1) eth_.send(0x0030+i, ACCtrigMode);
            else                     eth_.send(0x0030+i, 0);
        }
	//ACDC hardware trigger
	unsigned int command = 0x00B00000;
	command = (command | (boardMask << 24)) | (unsigned short)ACDCtrigMode;
	eth_.send(0x100, command);
}

/*ID 20: Switch for the calibration input on the ACC*/
void ACC::toggleCal(int onoff, unsigned int channelmask, unsigned int boardMask)
{
	unsigned int command = 0x00C00000;
	//the firmware just uses the channel mask to toggle
	//switch lines. So if the cal is off, all channel lines
	//are set to be off. Else, uses channel mask
	if(onoff == 1)
	{
		//channelmas is default 0x7FFF
		command = (command | (boardMask << 24)) | channelmask;
                eth_.send(0x100, 0x00c10001 | (boardMask << 24));
	}else if(onoff == 0)
	{
		command = (command | (boardMask << 24));
                eth_.send(0x100, 0x00c10000 | (boardMask << 24));
	}
        eth_.send(0x100, command);


}


// void ACC::writeThread()
// {
//     for(ACDC& acdc: acdcs_)
//     {
//         acdc.setNEvents(0);
//     }

//     nEvtsMax_ = 0;
//     int evt = 0;
//     int consequentErrors = 0;
//     while(runWriteThread_ && (nEvtsMax_ < params_.eventNumber || params_.eventNumber < 0))
//     {
//         std::vector<uint64_t> acdc_data = eth_burst_.recieve_burst(1445);
//         ++evt;
//         if((acdc_data[0]&0xffffffffffffff00) == 0x123456789abcde00 && 
//            (acdc_data[1]&0xffff000000000000) == 0xac9c000000000000)
//         {
//             int data_bi = acdc_data[0] & 0xff;

//             for(ACDC& acdc: acdcs_)
//             {
//                 //base command for set data readmode and which board bi to read
//                 int bi = acdc.getBoardIndex();

//                 if(data_bi == bi)
//                 {
//                     acdc.writeRawDataToFile(acdc_data);
//                     acdc.incNEvents();
//                     break;
//                 }
//             }
//             consequentErrors = 0;
//         }
//         else
//         {
//             std::cout << "Header error: " << acdc_data[0] << "\t" << consequentErrors << "\n";
//             //versionCheck(true);
//             int i = 0;
//             int i_Stop = 99999999;
//             for(auto& datum : acdc_data)
//             {
//                 printf("%5i: %16lx\n", i, datum);
//                 if((datum&0xffffffffffffff00) == 0x123456789abcde00) 
//                 {
//                     std::cout << "WEEEEHOOOOO: " << i << "\t" << evt << "\t" << nEvtsMax_ << "\t" << acdc_data.size() << "\n";
//                     i_Stop = i + 8;
//                 }
//                 ++i;
//                 if(i == i_Stop) break;
//             }
//             resetLinks();
//             //std::vector<uint64_t> acdc_data = eth_burst_.recieve_burst(i);
//             //std::cout << "Read: " << acdc_data.size() << std::endl;
//             ++consequentErrors;
//             if(consequentErrors >= 2)
//             {
//                 //try flushing data until relaigned
//                 for(int i = 0; i < 15; ++i)
//                 {
//                     std::vector<uint64_t> acdc_data = eth_burst_.recieve_burst(2);
//                     if((acdc_data[0]&0xffffffffffffff00) == 0x123456789abcde00 && 
//                        (acdc_data[1]&0xffff000000000000) == 0xac9c000000000000)
//                     {
//                         //we found a header, slurp up the rest of this event
//                         eth_burst_.recieve_burst(1445-182);
//                     }
//                 }
//             }
//             else if(consequentErrors >= 4) break;
//         }

        
//         const auto& nEvtsMaxPtr = std::max_element(acdcs_.begin(), acdcs_.end(), [](const ACDC& a, const ACDC& b){return a.getNEvents() < b.getNEvents();});
//         if(nEvtsMaxPtr == acdcs_.end())
//         {
//             //THROW ERROR HERE
//             break;
//         }
//         nEvtsMax_ = nEvtsMaxPtr->getNEvents();
//     }
// }


std::vector<std::vector<uint64_t>> ACC::transmitData()
{
for(ACDC& acdc: acdcs_)
    {
        acdc.setNEvents(0);
    }

    nEvtsMax_ = 0;
    std::vector<std::vector<uint64_t>> all_data;
    int evt = 0;
    int consequentErrors = 0;
    // debug
    cout << "ACDC vector size: " << acdcs_.size() << endl;

    while( (nEvtsMax_ < params_.eventNumber || params_.eventNumber < 0) && running_)
    {
        std::vector<uint64_t> acdc_data = eth_burst_.recieve_burst(1445);
        ++evt;
        if((acdc_data[0]&0xffffffffffffff00) == 0x123456789abcde00 && 
           (acdc_data[1]&0xffff000000000000) == 0xac9c000000000000)
        {
            int data_bi = acdc_data[0] & 0xff;

            for(ACDC& acdc: acdcs_)
            {
                //base command for set data readmode and which board bi to read
                int bi = acdc.getBoardIndex();

                if(data_bi == bi)
                {
                    acdc.incNEvents();
                    all_data.push_back(acdc_data);
                    break;
                }
            }
            consequentErrors = 0;
        }
        else
        {std::cout << "Header error: " << acdc_data[0] << "\t" << consequentErrors << "\n";
            //versionCheck(true);
            int i = 0;
            int i_Stop = 99999999;
            for(auto& datum : acdc_data)
            {
                printf("%5i: %16lx\n", i, datum);
                if((datum&0xffffffffffffff00) == 0x123456789abcde00) 
                {
                    std::cout << "WEEEEHOOOOO: " << i << "\t" << evt << "\t" << nEvtsMax_ << "\t" << acdc_data.size() << "\n";
                    i_Stop = i + 8;
                }
                ++i;
                if(i == i_Stop) break;
            }
            resetLinks();
            //std::vector<uint64_t> acdc_data = eth_burst_.recieve_burst(i);
            //std::cout << "Read: " << acdc_data.size() << std::endl;
            ++consequentErrors;
            if(consequentErrors >= 2)
            {
                //try flushing data until relaigned
                for(int i = 0; i < 15; ++i)
                {
                    std::vector<uint64_t> acdc_data = eth_burst_.recieve_burst(2);
                    if((acdc_data[0]&0xffffffffffffff00) == 0x123456789abcde00 && 
                       (acdc_data[1]&0xffff000000000000) == 0xac9c000000000000)
                    {
                        //we found a header, slurp up the rest of this event
                        eth_burst_.recieve_burst(1445-182);
                    }
                }
            }
            else if(consequentErrors >= 4) {cout << "Loop: " << evt 
                << ", nEvtsMax_: " << nEvtsMax_ 
                << ", running_: " << running_
                << ", consequentErrors: " << consequentErrors 
                << endl;
            break;} 
        }

        
        const auto& nEvtsMaxPtr = std::max_element(acdcs_.begin(), acdcs_.end(), [](const ACDC& a, const ACDC& b){return a.getNEvents() < b.getNEvents();});
        if(nEvtsMaxPtr == acdcs_.end())
        {
            cout << "NO ACDC"<< endl;
            //THROW ERROR HERE
            break;
        }
        nEvtsMax_ = nEvtsMaxPtr->getNEvents();
    }

    return all_data;
}
/*------------------------------------------------------------------------------------*/
/*---------------------------Read functions listening for data------------------------*/

/*ID 15: Main listen fuction for data readout. Runs for 5s before retuning a negative*/
int ACC::listenForAcdcData()
{
//    //setup a sigint capturer to safely
//    //reset the boards if a ctrl-c signal is found
//    struct sigaction sa;
//    memset( &sa, 0, sizeof(sa) );
//    sa.sa_handler = got_signal;
//    sigfillset(&sa.sa_mask);
//    sigaction(SIGINT,&sa,NULL);

    int eventCounter = 0;
    if(params_.triggerMode == 1)
    {
    while(eventCounter<params_.eventNumber)
    {
        ++eventCounter;
            softwareTrigger();

            //ensure we are past the 80 us PSEC read time
            usleep(350);

        }
    }

    //endRun();

    //if(nEvtsMax_ < params_.eventNumber - 1) printf("Events Missing\n");


    return 0;
}

void ACC::startDAQThread()
{
    daq_thread_.reset(new std::thread(&ACC::listenForAcdcData, this));
}

void ACC::joinDAQThread()
{
    if(daq_thread_) daq_thread_->join();
}


void ACC::resetLinks()
{
//    setHardwareTrigSrc(0, params_.boardMask);
//    enableTransfer(0);
    eth_.send(0x23, 0);

    usleep(100);
    dumpData(params_.boardMask);

//    enableTransfer(3);
    eth_.send(0x23, 1);
//    setHardwareTrigSrc(params_.triggerMode,params_.boardMask);
    
}


/*ID 27: Turn off triggers and data transfer off */
void ACC::endRun()
{
    setHardwareTrigSrc(0, params_.boardMask);
    enableTransfer(0);
    eth_.send(0x23, 0);
    usleep(100);
    eth_.setBurstMode(false);
}



/*------------------------------------------------------------------------------------*/
/*---------------------------Active functions for informations------------------------*/

/*ID 19: Pedestal setting procedure.*/
bool ACC::setPedestals(unsigned int boardmask, unsigned int chipmask, unsigned int adc)
{
    for(int iChip = 0; iChip < 5; ++iChip)
    {
	if(chipmask & (0x01 << iChip))
	{
	    unsigned int command = 0x00A20000;
	    command = (command | (boardmask << 24) ) | (iChip << 12) | adc;
            eth_.send(0x100, command);
	}
    }
    return true;
}


bool ACC::setPedestals(unsigned int boardmask, const std::vector<unsigned int>& pedestals)
{
    if(pedestals.size() != 5) 
    {
        writeErrorLog("Incorrect number of pedestal values (" + std::to_string(pedestals.size()) + ") specified!!!");
        return false;
    }

    for(int iChip = 0; iChip < 5; ++iChip)
    {
        unsigned int command = 0x00A20000;
        command = (command | (boardmask << 24) ) | (iChip << 12) | pedestals[iChip];
        eth_.send(0x100, command);
    }
    return true;
}


void ACC::setVddDLL(const std::vector<uint32_t>& vdd_dll_vec, bool resetDLL)
{
    //set dll_vdd
    for(const auto& acdc : acdcs_)
    {
        for(int iPSEC = 0; iPSEC < 5; ++iPSEC)
        {
            printf("%08x\n", 0x00A00000 | (1 << (acdc.getBoardIndex() + 24)) | (iPSEC << 12) | vdd_dll_vec[iPSEC]);
            eth_.send(0x100, 0x00A00000 | (1 << (acdc.getBoardIndex() + 24)) | (iPSEC << 12) | vdd_dll_vec[iPSEC]);
        }

        //reset dll
        if(resetDLL) eth_.send(0x100, 0x00F20000 | (1 << (acdc.getBoardIndex() + 24)));
    }
}

/*ID 24: Special function to check connected ACDCs for their firmware version*/ 
std::string ACC::versionCheck(bool debug)
{
    std::stringstream ss;

    std::string version;

    unsigned int command;

    auto AccBuffer = eth_.recieve_many(0x1000, 32);
    if(AccBuffer.size()==32)
    {

        ss << "ACC has the firmware version: " << std::hex << AccBuffer.at(0) << std::dec;
        uint16_t year  = (AccBuffer[1] >> 16) & 0xffff;
        uint16_t month = (AccBuffer[1] >>  8) & 0xff;
        uint16_t day   = (AccBuffer[1] >>  0) & 0xff;
        ss << " from " << std::hex << month << "/" << std::hex << day << "/" << std::hex << year << std::endl;
        std::string accVersion = ss.str();
	if(debug)
	{
        char buf[512];


	     auto eAccBuffer = eth_.recieve_many(0x1100, 64+32);

	    snprintf(buf, sizeof(buf), "  PLL lock status:\n    System PLL: %d\n    Serial PLL: %d\n    DPA PLL 1:  %d\n    DPA PLL 2:  %d\n", (AccBuffer[2] & 0x1)?1:0, (AccBuffer[2] & 0x2)?1:0, (AccBuffer[2] & 0x4)?1:0, (AccBuffer[2] & 0x8)?1:0);
        version += buf;
	    snprintf(buf, sizeof(buf), "  %-30s %10s %10s %10s %10s %10s %10s %10s %10s\n", "", "ACDC0", "ACDC1", "ACDC2", "ACDC3", "ACDC4", "ACDC5", "ACDC6", "ACDC7");
	    version += buf;
        snprintf(buf, sizeof(buf),"  %-30s %10d %10d %10d %10d %10d %10d %10d %10d\n", "40 MPBS link rx clk fail", (AccBuffer[16] & 0x1)?1:0, (AccBuffer[16] & 0x2)?1:0, (AccBuffer[16] & 0x4)?1:0, (AccBuffer[16] & 0x8)?1:0, (AccBuffer[16] & 0x10)?1:0, (AccBuffer[16] & 0x20)?1:0, (AccBuffer[16] & 0x40)?1:0, (AccBuffer[16] & 0x80)?1:0);
	    version += buf;
        snprintf(buf, sizeof(buf),"  %-30s %10d %10d %10d %10d %10d %10d %10d %10d\n", "40 MPBS link align err", (AccBuffer[17] & 0x1)?1:0, (AccBuffer[17] & 0x2)?1:0, (AccBuffer[17] & 0x4)?1:0, (AccBuffer[17] & 0x8)?1:0, (AccBuffer[17] & 0x10)?1:0, (AccBuffer[17] & 0x20)?1:0, (AccBuffer[17] & 0x40)?1:0, (AccBuffer[17] & 0x80)?1:0);
	    version += buf;
        snprintf(buf, sizeof(buf),"  %-30s %10d %10d %10d %10d %10d %10d %10d %10d\n", "40 MPBS link decode err", (AccBuffer[18] & 0x1)?1:0, (AccBuffer[18] & 0x2)?1:0, (AccBuffer[18] & 0x4)?1:0, (AccBuffer[18] & 0x8)?1:0, (AccBuffer[18] & 0x10)?1:0, (AccBuffer[18] & 0x20)?1:0, (AccBuffer[18] & 0x40)?1:0, (AccBuffer[18] & 0x80)?1:0);
	    version += buf;
        snprintf(buf, sizeof(buf),"  %-30s %10d %10d %10d %10d %10d %10d %10d %10d\n", "40 MPBS link disparity err", (AccBuffer[19] & 0x1)?1:0, (AccBuffer[19] & 0x2)?1:0, (AccBuffer[19] & 0x4)?1:0, (AccBuffer[19] & 0x8)?1:0, (AccBuffer[19] & 0x10)?1:0, (AccBuffer[19] & 0x20)?1:0, (AccBuffer[19] & 0x40)?1:0, (AccBuffer[19] & 0x80)?1:0);
	    version += buf;
        snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "40 MPBS link Rx FIFO Occ", eAccBuffer[56], eAccBuffer[57], eAccBuffer[58], eAccBuffer[59], eAccBuffer[60], eAccBuffer[61], eAccBuffer[62], eAccBuffer[63]);
	    version += buf;
        snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "250 MPBS Byte FIFO 0 Occ", eAccBuffer[0], eAccBuffer[2], eAccBuffer[4], eAccBuffer[6], eAccBuffer[8], eAccBuffer[10], eAccBuffer[12], eAccBuffer[14]);
	    version += buf;
	    snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "250 MPBS Byte FIFO 1 Occ", eAccBuffer[1], eAccBuffer[3], eAccBuffer[5], eAccBuffer[7], eAccBuffer[9], eAccBuffer[11], eAccBuffer[13], eAccBuffer[15]);
	    version += buf;
	    snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "250 MPBS PRBS Err 0", eAccBuffer[16], eAccBuffer[18], eAccBuffer[20], eAccBuffer[22], eAccBuffer[24], eAccBuffer[26], eAccBuffer[28], eAccBuffer[30]);
	    version += buf;
	    snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "250 MPBS PRBS Err 1", eAccBuffer[17], eAccBuffer[19], eAccBuffer[21], eAccBuffer[23], eAccBuffer[25], eAccBuffer[27], eAccBuffer[29], eAccBuffer[31]);
	    version += buf;
	    snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "250 MPBS Symbol Err 0", eAccBuffer[32], eAccBuffer[34], eAccBuffer[36], eAccBuffer[38], eAccBuffer[40], eAccBuffer[42], eAccBuffer[44], eAccBuffer[46]);
	    version += buf;
	    snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "250 MPBS Symbol Err 1", eAccBuffer[33], eAccBuffer[35], eAccBuffer[37], eAccBuffer[39], eAccBuffer[41], eAccBuffer[43], eAccBuffer[45], eAccBuffer[47]);
	    version += buf;
	    snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "250 MPBS parity Err 0", eAccBuffer[64], eAccBuffer[66], eAccBuffer[68], eAccBuffer[70], eAccBuffer[72], eAccBuffer[74], eAccBuffer[76], eAccBuffer[78]);
	    version += buf;
	    snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "250 MPBS parity Err 1", eAccBuffer[65], eAccBuffer[67], eAccBuffer[69], eAccBuffer[71], eAccBuffer[73], eAccBuffer[75], eAccBuffer[77], eAccBuffer[79]);
	    version += buf;
	    snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "250 MBPS FIFO Occ", eAccBuffer[48], eAccBuffer[49], eAccBuffer[50], eAccBuffer[51], eAccBuffer[52], eAccBuffer[53], eAccBuffer[54], eAccBuffer[55]);
	    version += buf;
	    snprintf(buf, sizeof(buf),"  %-30s %10lu %10lu %10lu %10lu %10lu %10lu %10lu %10lu\n", "Self trig count", eAccBuffer[80], eAccBuffer[81], eAccBuffer[82], eAccBuffer[83], eAccBuffer[84], eAccBuffer[85], eAccBuffer[86], eAccBuffer[87]);
	    version += buf;
	    //for(auto& val : lastAccBuffer) printf("%016lx\n", val);
	    //for(unsigned int i = 0; i < lastAccBuffer.size(); ++i) printf("stuff: 11%02x: %10ld\n", i, lastAccBuffer[i]);
	}
    }
    else
    {
        ss << "ACC got the no info frame" << std::endl;
        std::string accVersion = ss.str();
        version += accVersion;
    }

    //for(int i = 0; i < 16; ++i) printf("byteFIFO occ %5d: %10d\n", i, eth_.recieve(0x1100+i));

    //flush ACDC slow control FIFOs 
    eth_.send(0x2, 0xff);
	
    //Request ACDC info frame 
    command = 0xFFD00000; 
    eth_.send(0x100, command);

    usleep(500);

    //Loop over the ACC buffer words that show the ACDC buffer size
    //32 words represent a connected ACDC
    for(int i = 0; i < MAX_NUM_BOARDS; i++)
    {
        uint64_t bufLen = eth_.recieve(0x1138+i);
        if(bufLen > 5)
        {
            std::vector<uint64_t> buf = eth_.recieve_many(0x1200+i, bufLen, EthernetInterface::NO_ADDR_INC);
            ss << "Board " << i << " has the firmware version: " << std::hex << buf.at(2) << std::dec;
            ss << " from " << std::hex << ((buf.at(4) >> 8) & 0xff) << std::dec << "/" << std::hex << (buf.at(4) & 0xff) << std::dec << "/" << std::hex << buf.at(3) << std::dec << std::endl;
            std::string accVersion = ss.str();
            version += accVersion;
	    if(debug)
	    {

        char buf[216];
		snprintf(buf, sizeof(buf),"  Header/footer: %4lx %4lx %4lx %4lx (%s)\n", buf[0], buf[1], buf[30], buf[31], (buf[0] == 0x1234 && buf[1] == 0xbbbb && buf[30] == 0xbbbb && buf[31] == 0x4321)?"Correct":"Wrong");
		version += buf;
        snprintf(buf, sizeof(buf),"  PLL lock status:\n    ACC PLL:    %d\n    Serial PLL: %d\n    JC PLL:     %d\n    SYS PLL:    %d\n    WR PLL:     %d\n", (buf[6] & 0x4)?1:0, (buf[6] & 0x2)?1:0, (buf[6] & 0x200)?1:0, (buf[6] & 0x8)?1:0, (buf[6] & 0x1)?1:0);
        version += buf;
                snprintf(buf, sizeof(buf),"  FLL Locks:              %8lx\n", (buf[6] >> 4)&0x1f);
                version += buf;
                snprintf(buf, sizeof(buf),"  ACC 40 MHz:             %8.3f MHz\n", float(buf[7])/1000.0);
                version += buf;
                snprintf(buf, sizeof(buf),"  JCPLL 40 MHz:           %8.3f MHz\n", float(buf[8])/1000.0);
                version += buf;
		snprintf(buf, sizeof(buf),"  Backpressure:           %8d\n", (buf[5] & 0x2)?1:0);
        version += buf;
		snprintf(buf, sizeof(buf),"  40 MBPS parity error:   %8d\n", (buf[5] & 0x1)?1:0);
        version += buf;
		snprintf(buf, sizeof(buf),"  Event count:            %8lu\n", (buf[15] << 16) | buf[16]);
        version += buf;
		snprintf(buf, sizeof(buf),"  ID Frame count:         %8lu\n", (buf[17] << 16) | buf[18]);
        version += buf;
		snprintf(buf, sizeof(buf),"  Trigger count all:      %8lu\n", (buf[11] << 16) | buf[12]);
        version += buf;
		snprintf(buf, sizeof(buf),"  Trigger count accepted: %8lu\n", (buf[13] << 16) | buf[14]);
        version += buf;
		snprintf(buf, sizeof(buf),"  PSEC0 FIFO Occ:         %8lu\n", buf[21]);
        version += buf;
		snprintf(buf, sizeof(buf),"  PSEC1 FIFO Occ:         %8lu\n", buf[22]);
        version += buf;
		snprintf(buf, sizeof(buf),"  PSEC2 FIFO Occ:         %8lu\n", buf[23]);
        version += buf;
		snprintf(buf, sizeof(buf),"  PSEC3 FIFO Occ:         %8lu\n", buf[24]);
        version += buf;
		snprintf(buf, sizeof(buf),"  PSEC4 FIFO Occ:         %8lu\n", buf[25]);
        version += buf;
		snprintf(buf, sizeof(buf),"  Wr time FIFO Occ:       %8lu\n", buf[26]);
		version += buf;
		snprintf(buf, sizeof(buf),"  Sys time FIFO Occ:      %8lu\n", buf[27]);
		version += buf;
		snprintf(buf, sizeof(buf),"\n");
		version += buf;

                std::vector<std::vector<uint64_t>> bufs;
                for(int j = 0; j < 5; ++j)
                {
                    command = (0x01000000 << i) | (0x00D00001 + j); 
                    eth_.send(0x100, command);

                    usleep(500);
                    uint64_t bufLen = eth_.recieve(0x1138+i);
                    if(bufLen >= 32)
                    {
                        bufs.push_back(eth_.recieve_many(0x1200+i, bufLen, EthernetInterface::NO_ADDR_INC));
                    }
                }
                snprintf(buf, sizeof(buf),"    PSEC4:                      %8ld  %8ld  %8ld  %8ld  %8ld\n", bufs[0][16], bufs[1][16], bufs[2][16], bufs[3][16], bufs[4][16]); 
                version += buf;
                snprintf(buf, sizeof(buf),"    RO Feedback count:          %8ld  %8ld  %8ld  %8ld  %8ld\n", bufs[0][3], bufs[1][3], bufs[2][3], bufs[3][3], bufs[4][3]); 
                version += buf;
                snprintf(buf, sizeof(buf),"    RO Feedback target:         %8ld  %8ld  %8ld  %8ld  %8ld\n", bufs[0][4], bufs[1][4], bufs[2][4], bufs[3][4], bufs[4][4]);
                version += buf;
                snprintf(buf, sizeof(buf),"    pro Vdd:                    %8ld  %8ld  %8ld  %8ld  %8ld\n", bufs[0][7], bufs[1][7], bufs[2][7], bufs[3][7], bufs[4][7]); 
                version += buf;
                snprintf(buf, sizeof(buf),"    Vbias:                      %8ld  %8ld  %8ld  %8ld  %8ld\n", bufs[0][5], bufs[1][5], bufs[2][5], bufs[3][5], bufs[4][5]);
                version += buf;
                snprintf(buf, sizeof(buf),"    Self trigger threshold 0:   %8ld  %8ld  %8ld  %8ld  %8ld\n", bufs[0][6], bufs[1][6], bufs[2][6], bufs[3][6], bufs[4][6]); 
                version += buf;
                snprintf(buf, sizeof(buf),"    vcdl count:                 %8ld  %8ld  %8ld  %8ld  %8ld\n", (bufs[0][14] << 16) | bufs[0][13], (bufs[1][14] << 16) | bufs[1][13], (bufs[2][14] << 16) | bufs[2][13], (bufs[3][14] << 16) | bufs[3][13], (bufs[4][14] << 16) | bufs[4][13]); 
                version += buf;
                snprintf(buf, sizeof(buf),"    DLL Vdd:                    %8ld  %8ld  %8ld  %8ld  %8ld\n", bufs[0][15], bufs[1][15], bufs[2][15], bufs[3][15], bufs[4][15]); 
                version += buf;
                snprintf(buf, sizeof(buf),"\n");
                version += buf;

            }
                //printf("bufLen: %ld\n", bufLen);
            //for(unsigned int j = 0; j < buf.size(); ++j) printf("%3i: %016lx\n", j, buf[j]);
        }
        else
        {
            ss << "Board " << i << " is not connected" << std::endl;
            std::string accVersion = ss.str();
            version += accVersion;
        }
    }

    return version;
}


/*------------------------------------------------------------------------------------*/
/*-------------------------------------Help functions---------------------------------*/

/*ID 13: Fires the software trigger*/
void ACC::softwareTrigger()
{
	//Software trigger
        eth_.send(0x0010, 0xff);
}

/*ID 16: Used to dis/enable transfer data from the PSEC chips to the buffers*/
void ACC::enableTransfer(int onoff)
{
    unsigned int command;
    command = 0xFFF60000;
    eth_.send(0x100, command + onoff);
}

/*ID 23: Wakes up the USB by requesting an ACC info frame*/
void ACC::usbWakeup()
{
//	unsigned int command = 0x00200000;
//	usbcheck=usb->sendData(command); if(usbcheck==false){writeErrorLog("Send Error");}
}

/*ID 18: Tells ACDCs to clear their ram.*/ 
void ACC::dumpData(unsigned int boardMask)
{
	//send and read.
        eth_.send(0x0001, boardMask);
}


/*ID 27: Resets the ACDCs*/
void ACC::resetACDC(unsigned int boardMask)
{
    unsigned int command = 0x00FF0000;
    eth_.send(0x100, command | (boardMask << 24));
    std::cout << "ACDCs were reset" << std::endl;
}

/*ID 28: Resets the ACCs*/
void ACC::resetACC()
{
    eth_.send(0x1ffffffff, 0x1);
    sleep(5);
    eth_.send(0x0000, 0x1);
    std::cout << "ACC was reset" << std::endl;
}


/*------------------------------------------------------------------------------------*/
/*-------------------------------------Help functions---------------------------------*/

/*ID 29: Write function for the error log*/
void ACC::writeErrorLog(string errorMsg)
{
    string err = "errorlog.txt";
    cout << "------------------------------------------------------------" << endl;
    cout << errorMsg << endl;
    cout << "------------------------------------------------------------" << endl;
    ofstream os_err(err, ios_base::app);
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
}


/*ID 25: Scan possible high speed link clock phases and select the optimal phase setting*/ 
void ACC::scanLinkPhase(unsigned int boardMask, bool print)
{
    std::vector<std::vector<uint64_t>> errors;

    if(print)
    {
        printf("Phase  ");
        for(int iChan = 0; iChan < 8; iChan += 2) printf("%25s %2d %21s", "ACDC:", iChan/2, " ");
        printf("\n      ");
        for(int iChan = 0; iChan < 8; ++iChan) printf("%12s %2d          ", "Channel:", iChan%2);
        printf("\n      ");
        for(int iChan = 0; iChan < 8; ++iChan) printf(" %10s %9s    ", "Encode err", "PRBS err");
        printf("\n");
    }
    
    for(int iOffset = 0; iOffset < 24; ++iOffset)
    {
        // advance phase one step (there are 24 total steps in one clock cycle)
        eth_.send(0x0054, 0x0000);
        for(int iChan = 0; iChan < 8; ++iChan)
        {
            eth_.send(0x0055, 0x0000 + iChan);
            eth_.send(0x0056, 0x0000);
        }

        // transmit idle pattern to make sure link is aligned 
        enableTransfer(0);

        usleep(1000);

        //transmit PRBS pattern 
        enableTransfer(1);

        usleep(100);

        //reset error counters 
        eth_.send(0x0053, 0x0000);

        usleep(1000);

        std::vector<uint64_t> decode_errors = eth_.recieve_many(0x1120, 8);
        if(print)
        {
            std::vector<uint64_t> prbs_errors = eth_.recieve_many(0x1110, 8);

            printf("%5d  ", iOffset);
            for(int iChan = 0; iChan < 8; ++iChan) printf("%10d %9d     ", uint32_t(decode_errors[iChan]), uint32_t(prbs_errors[iChan]));
            printf("\n");
        }

        errors.push_back(decode_errors);
    }

    // set transmitter back to idle mode
    enableTransfer(0);

    if(boardMask)
    {
        if(print) printf("Set:   "); 
        for(int iChan = 0; iChan < 8; ++iChan)
        {
            // set phase for channels in boardMask
            if(boardMask & (1 << iChan))
            {
                int stop = 0;
                int length = 0;
                int length_best = 0;
                for(int i = 0; i < int(2*errors.size()); ++i)
                {
                    int imod = i % errors.size();
                    if(errors[imod][iChan] == 0)
                    {
                        ++length;
                    }
                    else
                    {
                        if(length >= length_best)
                        {
                            stop = imod;
                            length_best = length;
                        }
                        length = 0;
//                if(i > int(errors.size())) break;
                    }
                }
                int phaseSetting = (stop - length_best/2)%errors.size();
                if(print) printf("%15d          ", phaseSetting);
                eth_.send(0x0054, 0x0000);
                eth_.send(0x0055,  iChan);
                for(int i = 0; i < phaseSetting; ++i)
                {
                    eth_.send(0x0056, 0x0000);	    
                }
            }
            else
            {
                if(print) printf("%25s", " ");
            }
        }
        if(print) printf("\n"); 

	// ensure at least 1 ms for links to realign (ensures at least 25 alignment markers)
	usleep(1000);

        //reset error counters 
        eth_.send(0x0053, 0x0000);
    }

}

void ACC::sendJCPLLSPIWord(unsigned int word, unsigned int boardMask, bool verbose)
{
    unsigned int clearRequest = 0x00F10000 | (boardMask << 24);
    unsigned int lower16 = 0x00F30000 | (boardMask << 24) | (0xFFFF & word);
    unsigned int upper16 = 0x00F40000 | (boardMask << 24) | (0xFFFF & (word >> 16));
    unsigned int setPLL = 0x00F50000 | (boardMask << 24);
    
    eth_.send(0x100, clearRequest);
    eth_.send(0x100, lower16);
    eth_.send(0x100, upper16);
    eth_.send(0x100, setPLL);
    eth_.send(0x100, clearRequest);

    if(verbose)
    {
	printf("send 0x%08x\n", lower16);
	printf("send 0x%08x\n", upper16);
	printf("send 0x%08x\n", setPLL);
    }
}

/*ID 26: Configure the jcPLL settings */
void ACC::configJCPLL(unsigned int boardMask)
{
    // program registers 0 and 1 with approperiate settings for 40 MHz output 
    //sendJCPLLSPIWord(0x55500060, boardMask); // 25 MHz input
    //sendJCPLLSPIWord(0x5557C060, boardMask); // 125 MHz input old settings
    sendJCPLLSPIWord(0x55518060, boardMask); // 125 MHz input
    usleep(2000);    
    //sendJCPLLSPIWord(0x83810001, boardMask); // 25 MHz input
    //sendJCPLLSPIWord(0xFF810081, boardMask); // 125 MHz input old settings
    sendJCPLLSPIWord(0xAF810081, boardMask); // 125 MHz input
    usleep(2000);

    // cycle "power down" to force VCO calibration 
    sendJCPLLSPIWord(0x00001802, boardMask);
    usleep(2000);
    sendJCPLLSPIWord(0x00001002, boardMask);
    usleep(2000);
    sendJCPLLSPIWord(0x00001802, boardMask);
    usleep(2000);

    // toggle sync bit to synchronize output clocks
    sendJCPLLSPIWord(0x0001802, boardMask);
    usleep(2000);
    sendJCPLLSPIWord(0x0000802, boardMask);
    usleep(2000);
    sendJCPLLSPIWord(0x0001802, boardMask);
    usleep(2000);

    // read register
//    sendJCPLLSPIWord(0x0000000e);
//    sendJCPLLSPIWord(0x00000000);

    // write register contents to EEPROM
    //sendJCPLLSPIWord(0x0000001f);

}
