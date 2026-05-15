// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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

#ifndef TONE_MAPPING_FUNCTIONS_H
#define TONE_MAPPING_FUNCTIONS_H

#define TONE_MAPPER_NONE 0
#define TONE_MAPPER_ACES_FAST 1
#define TONE_MAPPER_ACES_FILMIC 2
#define TONE_MAPPER_UCHIMURA 3
#define TONE_MAPPER_PBR_NEUTRAL 4
#define TONE_MAPPER_COUNT 5

#ifdef __GARDEN__
static const char* TONE_MAPPER_NAMES[TONE_MAPPER_COUNT] =
{
	"None", "ACES Fast", "ACES Filmic", "Uchimura", "PBR Neutral"
};
#endif

#endif // TONE_MAPPING_FUNCTIONS_H