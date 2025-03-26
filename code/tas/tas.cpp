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
		gi.Printf("TAS : file doesn't start with START, ignoring\n");
	}
	if (!(allActionBuffer.front() == "DONE"))
	{
		gi.Printf("TAS : file doesn't start with DONE, ignoring\n");
	}




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
				// 30sec of waiting time at the start, just because.
				std::this_thread::sleep_for(WORKER_SLEEP_TIME * 500 * 10);
				firstIteration = false;
				proofOfConcept();
			}
			if (strcmp(level.mapname, lastKnownMap)) // New map
			{
				strcpy(lastKnownMap, level.mapname);
				readTasFile();
			}
			makeOperationThisFrame();
			gamestateChanged = false;
		}
	}
}

void TAS::makeOperationThisFrame()
{
	
	return;
}

void TAS::sendInputsThisFrame()
{



	// At the very end, reset known 1 frame buffer
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

		cgi_SendConsoleCommand("+left 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		gi.cvar_set("cl_yawspeed", "280");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		gi.cvar_set("cl_yawspeed", "140");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		cgi_SendConsoleCommand("-left 68 0");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

	}
}