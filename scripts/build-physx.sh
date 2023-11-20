#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"
cd ../libraries/physx/physx

echo "Fixing PhysX configs..."

if [[ "$OSTYPE" =~ ^linux ]]; then
	sed -i "s/-Werror//g" source/compiler/cmake/linux/CMakeLists.txt
	sed -i 's@name="PX_BUILDSNIPPETS" value="True"@name="PX_BUILDSNIPPETS" value="False"@g' buildtools/presets/public/linux.xml
elif [[ "$OSTYPE" =~ ^msys ]]; then
	sed -i 's@name="PX_BUILDSNIPPETS" value="True"@name="PX_BUILDSNIPPETS" value="False"@g' buildtools/presets/public/linux.xml
	sed -i 's@name="PX_GENERATE_STATIC_LIBRARIES" value="False"@name="PX_GENERATE_STATIC_LIBRARIES" value="True"@g' buildtools/presets/public/vc17win64.xml
fi

echo "Configuring PhysX..."

if [[ "$OSTYPE" =~ ^linux ]]; then
    ./generate_projects.sh linux
elif [[ "$OSTYPE" =~ ^msys ]]; then
    ./generate_projects.sh win64
fi

status=$?

if [ $status -ne 0 ]; then
    echo "Failed to configure PhysX library."
    exit $status
fi

echo ""
echo "Building debug PhysX..."

if [[ "$OSTYPE" =~ ^linux ]]; then
    cmake --build compiler/linux-checked/ --config Debug
elif [[ "$OSTYPE" =~ ^msys ]]; then
    cmake --build compiler/win64-checked/ --config Debug
fi

status=$?

if [ $status -ne 0 ]; then
    echo "Failed to build debug PhysX library."
    exit $status
fi

echo ""
echo "Building release PhysX..."

if [[ "$OSTYPE" =~ ^linux ]]; then
    cmake --build compiler/linux-release/ --config Release
elif [[ "$OSTYPE" =~ ^msys ]]; then
    cmake --build compiler/win64-release/ --config Release
fi

status=$?

if [ $status -ne 0 ]; then
    echo "Failed to build release PhysX library."
    exit $status
fi

exit 0
