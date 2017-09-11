/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file main.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * Ethereum client.
 */

#include <ctime>
#include <memory>
#include <thread>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <evhttp.h>
#include <inttypes.h>
#include "MinerAux.h"
#include "BuildInfo.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace boost::algorithm;

time_t start;

MinerCLI *miner;

void help()
{
	cout
		<< "Usage ethminer [OPTIONS]" << endl
		<< "Options:" << endl << endl;
	MinerCLI::streamHelp(cout);
	cout
		<< "General Options:" << endl
		<< "    -v,--verbosity <0 - 9>  Set the log verbosity from 0 to 9 (default: 8)." << endl
		<< "    -V,--version  Show the version and exit." << endl
		<< "    -h,--help  Show this help message and exit." << endl
		;
	exit(0);
}

void version()
{
	cout << "ethminer version " << ETH_PROJECT_VERSION << endl;
	cout << "Build: " << ETH_BUILD_PLATFORM << "/" << ETH_BUILD_TYPE << endl;
	exit(0);
}

void apiHandler()
{
	if (!event_init())
	{
		cerr << "Failed to init libevent." << endl;
		return;
	}

	char const SrvAddress[] = "0.0.0.0";
	uint16_t SrvPort = 5555;
	unique_ptr<evhttp, decltype(&evhttp_free)> Server(evhttp_start(SrvAddress, SrvPort), &evhttp_free);
	if (!Server)
	{
		cerr << "Failed to init http server." << endl;
		return;
	}
	void (*OnReq)(evhttp_request *req, void *) = [] (evhttp_request *req, void *)
	{
		time_t now;
		string pool;
		double seconds;

		time(&now);
		seconds = difftime(now, start);

		auto *OutBuf = evhttp_request_get_output_buffer(req);
		if (!OutBuf)
			return;

		if (miner->getPort() == "")
			pool = miner->getUrl();
		else
			pool = miner->getUrl() + ":" + miner->getPort();

		evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "application/json; charset=utf-8");
		evbuffer_add_printf(OutBuf, "{\"pool\": \"%s\", \"uptime\": %.0f,\"hashrate\": %ju}", pool.c_str(), seconds, miner->getRate());
		evhttp_send_reply(req, HTTP_OK, "", OutBuf);
	};

	evhttp_set_gencb(Server.get(), OnReq, nullptr);
	if (event_dispatch() == -1)
	{
		cerr << "Failed to run message loop." << endl;
		return;
	}

	return;
}

int main(int argc, char** argv)
{
	bool join;
	thread apiHandlerThread;

	time(&start);

	// Set env vars controlling GPU driver behavior.
	setenv("GPU_MAX_HEAP_SIZE", "100");
	setenv("GPU_MAX_ALLOC_PERCENT", "100");
	setenv("GPU_SINGLE_ALLOC_PERCENT", "100");

	miner = new MinerCLI(MinerCLI::OperationMode::Farm);

	for (int i = 1; i < argc; ++i)
	{
		// Mining options:
		if (miner->interpretOption(i, argc, argv))
			continue;

		// Standard options:
		string arg = argv[i];
		if ((arg == "-v" || arg == "--verbosity") && i + 1 < argc)
			g_logVerbosity = atoi(argv[++i]);
		else if (arg == "-h" || arg == "--help")
			help();
		else if (arg == "-V" || arg == "--version")
			version();
		else if (arg == "-l" || arg == "--listen")
		{
			join = true;
			apiHandlerThread = thread(apiHandler);
		}
		else
		{
			cerr << "Invalid argument: " << arg << endl;
			exit(-1);
		}
	}

	miner->execute();

	if(join) {
		apiHandlerThread.join();
	}

	return 0;
}
