// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "garden/json2bson.hpp"
#include "garden/thread-pool.hpp"
#include "garden/file.hpp"

#include "nlohmann/json.hpp"

#include <atomic>
#include <fstream>
#include <iostream>

using namespace garden;
using json = nlohmann::json;

#if GARDEN_DEBUG || defined(JSON2BSON)
bool Json2Bson::convertFile(const fs::path& filePath, const fs::path& inputPath, const fs::path& outputPath)
{	
	GARDEN_ASSERT(!filePath.empty());

	std::ifstream inputStream(inputPath / filePath);
	if (!inputStream.is_open())
		return false;

	json textData;
	inputStream >> textData;
	inputStream.close();

	auto binaryData = json::to_bson(textData);

	auto outputFilePath = outputPath / filePath;
	auto outputDirectory = outputFilePath.parent_path();
	if (!fs::exists(outputDirectory))
		fs::create_directories(outputDirectory);
	File::storeBinary(outputFilePath, binaryData);
	return true;
}
#endif

#ifdef JSON2BSON
//******************************************************************************************************************
int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		cout << "json2bson: error: no file name\n";
		return EXIT_FAILURE;
	}

	int logOffset = 1;
	fs::path workingPath = fs::path(argv[0]).parent_path();
	auto inputPath = workingPath, outputPath = workingPath;
	ThreadPool* threadPool = nullptr; atomic_int convertResult = true;
	
	for (int i = 1; i < argc; i++)
	{
		auto arg = argv[i];
		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
		{
			cout << "(C) 2022-" GARDEN_CURRENT_YEAR " Nikita Fediuchin. All rights reserved.\n"
				"json2bson - JSON to binary JSON file converter.\n"
				"\n"
				"Usage: json2bson [options] name...\n"
				"\n"
				"Options:\n"
				"  -i <dir>      Read input from <dir>.\n"
				"  -o <dir>      Write output to <dir>.\n"
				"  -t <value>    Specify thread pool size. (Uses all cores by default)\n"
				"  -h            Display available options.\n"
				"  --help        Display available options.\n"
				"  --version     Display converter version information.\n";
			return EXIT_SUCCESS;
		}
		else if (strcmp(arg, "--version") == 0)
		{
			cout << "json2bson " GARDEN_VERSION_STRING "\n";
			return EXIT_SUCCESS;
		}
		else if (strcmp(arg, "-i") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "json2bson: error: no input directory\n";
				return EXIT_FAILURE;
			}

			inputPath = argv[i + 1];
			logOffset += 2; i++;
		}
		else if (strcmp(arg, "-o") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "json2bson: error: no output directory\n";
				return EXIT_FAILURE;
			}

			outputPath = argv[i + 1];
			logOffset += 2; i++;
		}
		else if (strcmp(arg, "-t") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "json2bson: error: no thread count\n";
				return EXIT_FAILURE;
			}

			auto count = atoi(argv[i + 1]);
			if (count > 0 && count < thread::hardware_concurrency())
			{
				if (threadPool)
				{
					threadPool->wait();
					delete threadPool;
				}
				threadPool = new ThreadPool(false, "T", count);
			}
			logOffset += 2; i++;
		}
		else if (arg[0] == '-')
		{
			cout << "json2bson: error: unsupported option: '" << arg << "'\n";
			return EXIT_FAILURE;
		}
		else
		{
			if (!threadPool)
				threadPool = new ThreadPool(false, "T");
			threadPool->addTask([=, &convertResult](const ThreadPool::Task& task)
			{
				if (!convertResult)
					return;

				// Note: Sending one batched message due to multithreading.
				cout << string("Converting ") + arg + "\n" << flush;

				try
				{
					auto result = Json2Bson::convertFile(arg, inputPath, outputPath);
					if (!result)
						cout << string("json2bson: error: no file found (") + arg + ")\n";
					convertResult &= result;
				}
				catch (const json::parse_error& e)
				{
					cout << string("json2bson: ") + e.what() + " (" + arg + ")\n";
					convertResult &= false;
				}
			});
		}
	}

	if (threadPool)
	{
		threadPool->wait();
		delete threadPool;
	}
	return convertResult ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif