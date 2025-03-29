#pragma once

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>


class TAS {
public:
	TAS();
	~TAS();
	void start();
	void stop();
	void notifyNewFrame();
	
private:
	void infiniteLoop();
	void makeOperationThisFrame();
	void readTasFile();
	void proofOfConcept();

	bool gamestateChanged = false;
	bool firstIteration = true;
	bool sleepBufferHasBeenIt = false;

	std::atomic<bool> terminateThread = false;
	std::thread workerThread;

	int32_t starterFrame = 0;
	int32_t currentFrame = 0;
	int32_t sleepDuration = 0;
	char lastKnownMap [64] = "";

	std::vector<std::string> allCommandsBuffer;
	int16_t commandsBufferIndex = 0;
	std::string commandsBuffer = "";

};
