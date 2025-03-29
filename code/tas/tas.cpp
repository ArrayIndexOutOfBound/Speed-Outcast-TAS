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
	allCommandsBuffer.clear(); // Clear precedent buffer from other map
	commandsBufferIndex = 0;
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
			allCommandsBuffer.push_back(line);
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
	if (!(allCommandsBuffer.front() == "START"))
	{
		gi.Printf("TAS : file doesn't start with START, ignoring.\n");
		allCommandsBuffer.clear();
	}
	if (!(allCommandsBuffer.back() == "DONE"))
	{
		gi.Printf("TAS : file doesn't finish with DONE, ignoring.\n");
		allCommandsBuffer.clear();
	}
	for (int i = 0; i < allCommandsBuffer.size(); i++)
	{
		if (allCommandsBuffer[i] == "SLEEPS" || allCommandsBuffer[i] == "SLEEPF")
		{
			bool isNumber = !allCommandsBuffer[i+1].empty() && std::all_of(allCommandsBuffer[i+1].begin(), allCommandsBuffer[i+1].end(), ::isdigit);
			if (!isNumber)
			{
				gi.Printf("TAS : line after a sleep is not a number, ignoring.\n");
				allCommandsBuffer.clear();
			}
		}
	}

	if (allCommandsBuffer.size() >=1) gi.Printf("TAS : script is valid, executing.\n");
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
	if (allCommandsBuffer[commandsBufferIndex] == "START")
	{
		// Go up by one, we don't care about the start flag
		commandsBufferIndex++;
	}
	if (allCommandsBuffer[commandsBufferIndex] == "DONE")
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
		if (allCommandsBuffer[commandsBufferIndex] == "SLEEPS")
		{
			// Already checked before of this string is a integer
			sleepDuration = atoi(allCommandsBuffer[commandsBufferIndex + 1].c_str()) * 125; // 1 sec = 125frames
			commandsBufferIndex += 2;
			sleepBufferHasBeenIt = true;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SLEEPF")
		{
			// Already checked before of this string is a integer
			sleepDuration = atoi(allCommandsBuffer[commandsBufferIndex + 1].c_str());
			commandsBufferIndex += 2;
			sleepBufferHasBeenIt = true;

		}
		// Mouvement
		else if (allCommandsBuffer[commandsBufferIndex] == "ST_MOVE_F")
		{
			commandsBuffer += "+forward;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SP_MOVE_F")
		{
			commandsBuffer += "-forward;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "ST_MOVE_B")
		{
			commandsBuffer += "+back;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SP_MOVE_B")
		{
			commandsBuffer += "-back;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "ST_MOVE_L")
		{
			commandsBuffer += "+moveleft;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SP_MOVE_L")
		{
			commandsBuffer += "-moveleft;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "ST_MOVE_R")
		{
			commandsBuffer += "+moveright;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SP_MOVE_R")
		{
			commandsBuffer += "-moveright;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "ST_JUMP")
		{
			commandsBuffer += "+moveup;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SP_JUMP")
		{
			commandsBuffer += "-moveup;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "ST_CROUCH")
		{
			commandsBuffer += "+movedown;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SP_CROUCH")
		{
			commandsBuffer += "-movedown;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "ST_USE")
		{
			commandsBuffer += "+use;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SP_USE")
		{
			commandsBuffer += "-use;";
			commandsBufferIndex++;
		}
		// Mouse
		else if (allCommandsBuffer[commandsBufferIndex] == "ST_LC")
		{
			commandsBuffer += "+attack;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SP_LC")
		{
			commandsBuffer += "-attack;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "ST_RC")
		{
			commandsBuffer += "+altattack;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SP_RC")
		{
			commandsBuffer += "-altattack;";
			commandsBufferIndex++;
		}
		// Inventory
		else if (allCommandsBuffer[commandsBufferIndex] == "BACTA")
		{
			commandsBuffer += "use_bacta;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SEEKER")
		{
			commandsBuffer += "use_seeker;";
			commandsBufferIndex++;
		}
		// Force powers
		else if (allCommandsBuffer[commandsBufferIndex] == "FP_PUSH")
		{
			commandsBuffer += "force_push;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "FP_PULL")
		{
			commandsBuffer += "force_pull;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "FP_SPEED")
		{
			commandsBuffer += "force_speed;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "FP_DISTRACT")
		{
			commandsBuffer += "force_distract;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "FP_HEAL")
		{
			commandsBuffer += "force_heal;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "FP_SABER")
		{
			commandsBuffer += "saberAttackCycle;";
			commandsBufferIndex++;
		}
		// Game manipulation
		else if (allCommandsBuffer[commandsBufferIndex] == "QUICK_SAVE")
		{
			commandsBuffer += "save quik*;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "QUICK_LOAD")
		{
			commandsBuffer += "load quik;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "HARD_SAVE")
		{
			commandsBuffer += "save tas;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "HARD_LOAD")
		{
			commandsBuffer += "load tas;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "SOUNDSTOP")
		{
			commandsBuffer += "soundstop;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "VIDRESTART")
		{
			commandsBuffer += "vid_restart;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "PRINT")
		{
			gi.Printf("TAS : %s.\n", allCommandsBuffer[commandsBufferIndex + 1].c_str());
			commandsBufferIndex += 2;

		}
		// Weapon
		else if (allCommandsBuffer[commandsBufferIndex] == "WP_SABER")
		{
			commandsBuffer += "weapon 1;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "WP_BRYAR")
		{
			commandsBuffer += "weapon 2;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "WP_E11")
		{
			commandsBuffer += "weapon 3;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "WP_DISTRUPTOR")
		{
			commandsBuffer += "weapon 4;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "WP_BOWCASTER")
		{
			commandsBuffer += "weapon 5;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "WP_REPETITION")
		{
			commandsBuffer += "weapon 6;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "WP_DEMP")
		{
			commandsBuffer += "weapon 7;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "WP_FLECHETTE")
		{
			commandsBuffer += "weapon 8;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "WP_MISSILE")
		{
			commandsBuffer += "weapon 9;";
			commandsBufferIndex++;
		}
		else if (allCommandsBuffer[commandsBufferIndex] == "WP_EXPLOSIVES") // Note : thre must be a better way to properly get desired weapons
		{
			commandsBuffer += "weapon 10;";
			commandsBufferIndex++;
		}

	}
	
	// Four : Send the buffer during this frame
	cgi_SendConsoleCommand(commandsBuffer.c_str());
	sleepBufferHasBeenIt = false;
	commandsBuffer = "";
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