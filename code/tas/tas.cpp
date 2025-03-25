// leave this line at the top for all g_xxxx.cpp files...
#include "../game/g_headers.h"

#include "../game/q_shared.h"
#include "../game/g_local.h"

#include "tas.hpp"
#include <chrono>


constexpr std::chrono::milliseconds WORKER_SLEEP_TIME(5);
constexpr std::size_t FRAMETIME_BUFFER_SIZE = 1250;

TAS::TAS()
{
	terminateThread = false;
	starterFrame = 0;
	currentFrame = 0;
	//start();
}

TAS::~TAS() {
	stop();
}

void TAS::start() {
	terminateThread = false;
	if (!workerThread.joinable()) {
		workerThread = std::thread(&TAS::makeOperationThisFrame, this);
	}
}

void TAS::stop() {
	terminateThread = true;
	if (workerThread.joinable()) {
		workerThread.join();
	}
}

void TAS::readfromFile(std::string filename)
{
	return;
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
