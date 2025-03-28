// leave this line at the top for all g_xxxx.cpp files...
#include "../game/g_headers.h"
#include "../game/g_shared.h"

#include "../cgame/cg_headers.h"

#include "../game/q_shared.h"
#include "../game/g_local.h"

#include "../qcommon/qcommon.h"

#include "tas.hpp"
#include <chrono>



constexpr std::chrono::milliseconds WORKER_SLEEP_TIME(2);

//extern game_import_t gi;
//extern cg_t	cg;
//extern centity_t cg_entities[MAX_GENTITIES]; // Array at 0 is the player
//extern gentity_t g_entities[MAX_GENTITIES];
extern level_locals_t level;
//extern void	Cmd_ExecuteString(const char* text); // Linking error
//extern int com_frameNumber; // Linking error

/*
gi.cvar_set("skippingCinematic", "0");

void	cgi_SendClientCommand(const char* s) {
			syscall(CG_SENDCLIENTCOMMAND, s);
		}
		void	cgi_SendConsoleCommand(const char* text) {
			syscall(CG_SENDCONSOLECOMMAND, text);
		}

*/

TAS::TAS()
{
	terminateThread = false;
	starterFrame = 0;
	currentFrame = 0;
	start();
}

TAS::~TAS()
{
	
}

void TAS::start()
{
	terminateThread = false;
	if (!workerThread.joinable()) {
		workerThread = std::thread(&TAS::infiniteLoop, this);
	}
}

void TAS::stop()
{
	terminateThread = true;
	if (workerThread.joinable()) {
		workerThread.join();
	}
}

void TAS::notifyNewFrame() // From the game
{
	gamestateChanged = true;
	// TMaybe other things ?
}

void TAS::readTasFile()
{
	char* bufferFromFile = new char;
	//level.mapname + ".tas"
	string filename = lastKnownMap;
	filename.append(".tas");
	int length = gi.FS_ReadFile(filename.c_str(), (void**)&bufferFromFile);

	if (length == -1)
	{
		gi.Printf("TAS : error reading file, aborting !!!!!\n");
		gi.cvar_set("cg_TASActive", 0);
	}
	else
	{
		gi.Printf("TAS : file exists, processing...\n");
	}

	// TODO : store ALL actions in a massive string vector
	allActionBuffer.clear(); // Clear precedent buffer from other map
	actionBufferIndex = 0;
	//allActionBuffer.push_back("Test");



	string fileContent(bufferFromFile, length);
	std::istringstream stream(fileContent);
	string line;

	while (std::getline(stream, line))
	{
		// Handle Windows-style "\r\n" by removing '\r'
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		// Remove all lines that starts with '#' as they are comments
		if (!(line.front() == '#'))
		{
			allActionBuffer.push_back(line);
		}
		
	}

	// Debug
	
	/*
	for (int i = 0 ; i < allActionBuffer.size() ; i++)
	{
		gi.Printf("TAS_lines : %s\n", allActionBuffer[i].c_str());
	}
	*/

	// Small validations
	if (!(allActionBuffer.front() == "START"))
	{
		gi.Printf("TAS : file doesn't start with START, ignoring.\n");
		allActionBuffer.clear();
	}
	if (!(allActionBuffer.back() == "DONE"))
	{
		gi.Printf("TAS : file doesn't finish with DONE, ignoring.\n");
		allActionBuffer.clear();
	}
	for (int i = 0; i < allActionBuffer.size(); i++)
	{
		if (allActionBuffer[i] == "SLEEPS" || allActionBuffer[i] == "SLEEPF")
		{
			bool isNumber = !allActionBuffer[i+1].empty() && std::all_of(allActionBuffer[i+1].begin(), allActionBuffer[i+1].end(), ::isdigit);
			if (!isNumber)
			{
				gi.Printf("TAS : line after a sleep is not a number, ignoring.\n");
				allActionBuffer.clear();
			}
		}
	}

	if (allActionBuffer.size() >=1) gi.Printf("TAS : script is valid, executing.\n");
	gi.FS_FreeFile(bufferFromFile);
}

void TAS::infiniteLoop()
{
	while (!terminateThread)
	{
		std::this_thread::sleep_for(WORKER_SLEEP_TIME); // We sleep for 2ms while a frame is 8ms, we will ALWAYS hit a frame
		if (gamestateChanged) // Framenum increment 25 by 25 and starts at 8 ?!?!?!
		{
			// jk2gamex86.dll+5ACBC0
			if (firstIteration)
			{
				// 10sec of waiting time at the start, just because.
				std::this_thread::sleep_for(WORKER_SLEEP_TIME * 500 * 10);
				firstIteration = false;
				//proofOfConcept();
			}
			if (strcmp(level.mapname, lastKnownMap)) // New map
			{
				strcpy(lastKnownMap, level.mapname);
				readTasFile();
				// 1sec at the start of the level
				std::this_thread::sleep_for(WORKER_SLEEP_TIME * 500 * 1);
			}
			makeOperationThisFrame();
			gamestateChanged = false;
		}
	}
}

void TAS::makeOperationThisFrame()
{
	// First, check if we are currently sleeping
	if (sleepDuration > 0)
	{
		sleepDuration--;
		return;
	}
	// Second, check the START and DONE flags
	if (allActionBuffer[actionBufferIndex] == "START")
	{
		// Go up by one, we don't care about the start flag
		actionBufferIndex++;
	}
	if (allActionBuffer[actionBufferIndex] == "DONE")
	{
		//gi.Printf("TAS : end of execution for map %s.\n", lastKnownMap);
		//actionBufferIndex = 0;
		//commandBuffer = "";
		return;
	}

	// Third, the long one : build the buffer for the current frame.
	// A whole lot of ifelse, could be more beautiful but at least it should work as I wish it to be
	while (!sleepBufferHasBeenIt)
	{
		// Sleep
		if (allActionBuffer[actionBufferIndex] == "SLEEPS")
		{
			// Already checked before of this string is a integer
			sleepDuration = atoi(allActionBuffer[actionBufferIndex + 1].c_str()) * 125; // 1 sec = 125frames
			actionBufferIndex += 2;
			sleepBufferHasBeenIt = true;
		}
		else if (allActionBuffer[actionBufferIndex] == "SLEEPF")
		{
			// Already checked before of this string is a integer
			sleepDuration = atoi(allActionBuffer[actionBufferIndex + 1].c_str());
			actionBufferIndex += 2;
			sleepBufferHasBeenIt = true;

		}
		// Mouvement
		else if (allActionBuffer[actionBufferIndex] == "ST_MOVE_F")
		{
			commandBuffer += "+forward;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SP_MOVE_F")
		{
			commandBuffer += "-forward;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "ST_MOVE_B")
		{
			commandBuffer += "+back;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SP_MOVE_B")
		{
			commandBuffer += "-back;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "ST_MOVE_L")
		{
			commandBuffer += "+moveleft;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SP_MOVE_L")
		{
			commandBuffer += "-moveleft;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "ST_MOVE_R")
		{
			commandBuffer += "+moveright;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SP_MOVE_R")
		{
			commandBuffer += "-moveright;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "ST_JUMP")
		{
			commandBuffer += "+moveup;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SP_JUMP")
		{
			commandBuffer += "-moveup;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "ST_CROUCH")
		{
			commandBuffer += "+movedown;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SP_CROUCH")
		{
			commandBuffer += "-movedown;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "ST_USE")
		{
			commandBuffer += "+use;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SP_USE")
		{
			commandBuffer += "-use;";
			actionBufferIndex++;
		}
		// Mouse
		else if (allActionBuffer[actionBufferIndex] == "ST_LC")
		{
			commandBuffer += "+attack;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SP_LC")
		{
			commandBuffer += "-attack;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "ST_RC")
		{
			commandBuffer += "+altattack;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SP_RC")
		{
			commandBuffer += "-altattack;";
			actionBufferIndex++;
		}
		// Inventory
		else if (allActionBuffer[actionBufferIndex] == "BACTA")
		{
			commandBuffer += "use_bacta;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SEEKER")
		{
			commandBuffer += "use_seeker;";
			actionBufferIndex++;
		}
		// Force powers
		else if (allActionBuffer[actionBufferIndex] == "FP_PUSH")
		{
			commandBuffer += "force_push;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "FP_PULL")
		{
			commandBuffer += "force_pull;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "FP_SPEED")
		{
			commandBuffer += "force_speed;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "FP_DISTRACT")
		{
			commandBuffer += "force_distract;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "FP_HEAL")
		{
			commandBuffer += "force_heal;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "FP_SABER")
		{
			commandBuffer += "saberAttackCycle;";
			actionBufferIndex++;
		}
		// Game manipulation
		else if (allActionBuffer[actionBufferIndex] == "QUICK_SAVE")
		{
			commandBuffer += "save quik*;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "QUICK_LOAD")
		{
			commandBuffer += "load quik;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "HARD_SAVE")
		{
			commandBuffer += "save tas;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "HARD_LOAD")
		{
			commandBuffer += "load tas;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "SOUNDSTOP")
		{
			commandBuffer += "soundstop;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "VIDRESTART")
		{
			commandBuffer += "vid_restart;";
			actionBufferIndex++;
		}
		// Weapon
		else if (allActionBuffer[actionBufferIndex] == "WP_SABER")
		{
			commandBuffer += "weapon 1;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "WP_BRYAR")
		{
			commandBuffer += "weapon 2;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "WP_E11")
		{
			commandBuffer += "weapon 3;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "WP_DISTRUPTOR")
		{
			commandBuffer += "weapon 4;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "WP_BOWCASTER")
		{
			commandBuffer += "weapon 5;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "WP_REPETITION")
		{
			commandBuffer += "weapon 6;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "WP_DEMP")
		{
			commandBuffer += "weapon 7;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "WP_FLECHETTE")
		{
			commandBuffer += "weapon 8;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "WP_MISSILE")
		{
			commandBuffer += "weapon 9;";
			actionBufferIndex++;
		}
		else if (allActionBuffer[actionBufferIndex] == "WP_EXPLOSIVES") // Note : thre must be a better way to properly get desired weapons
		{
			commandBuffer += "weapon 10;";
			actionBufferIndex++;
		}

	}
	
	// Four : Send the buffer during this frame
	cgi_SendConsoleCommand(commandBuffer.c_str());
	sleepBufferHasBeenIt = false;
	commandBuffer = "";
	return;
}

void TAS::proofOfConcept()
{
	int breakpoint = 0;
	while (true)
	{
		//Cmd_ExecuteString("+moveright 68 387824171");
		//std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		//Cmd_ExecuteString("-moveright 68 0");
		//std::this_thread::sleep_for(std::chrono::milliseconds(1000));

		//gi.Printf("com_frameNumber : %i\n", com_frameNumber);
		//syscall( CG_SENDCONSOLECOMMAND, "cam_disable; set nextmap disconnect; cinematic outcast\n" );
		//cgi_SendClientCommand("sup");
		//cgi_SendConsoleCommand("sup");
		//cgi_SendConsoleCommand("+movedown 68 387824171");

		/*
		cgi_SendConsoleCommand("+moveright 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		cgi_SendConsoleCommand("-moveright 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		cgi_SendConsoleCommand("+back 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		cgi_SendConsoleCommand("-back 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		cgi_SendConsoleCommand("+moveleft 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		cgi_SendConsoleCommand("-moveleft 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		
		cgi_SendConsoleCommand("+forward 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(5000));
		cgi_SendConsoleCommand("-forward 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		cgi_SendConsoleCommand("+moveup 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		cgi_SendConsoleCommand("-moveup 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		cgi_SendConsoleCommand("+movedown 68 387824171");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		cgi_SendConsoleCommand("-movedown 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		*/

		cgi_SendConsoleCommand("+left");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		gi.cvar_set("cl_yawspeed", "280");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		gi.cvar_set("cl_yawspeed", "140");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		cgi_SendConsoleCommand("-left");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

	}
}