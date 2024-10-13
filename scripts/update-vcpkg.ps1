try {
    git --version > $null
} catch {
    Write-Host "Failed to get git version, please check if it's installed."
    Exit 1
}

if (Test-Path "C:\vcpkg") {
    try {
        git -C C:/vcpkg pull
    } catch {
        Write-Host "Failed to pull vcpkg repository changes."
        Exit 1
    }
} else {
    try {
        git clone https://github.com/microsoft/vcpkg C:/vcpkg
    } catch {
        Write-Host "Failed to clone vcpkg repository."
        Exit 1
    }
}

try {
    Set-Location -Path "C:\vcpkg"
} catch {
    Write-Host "Failed to change directory to vcpkg."
    Exit 1
}

try {
    .\bootstrap-vcpkg.bat
} catch {
    Write-Host "Failed to run vcpkg bootstrap script."
    Exit 1
}

$envPath = [Environment]::GetEnvironmentVariable("PATH", "Machine")

if ($envPath -notlike "*C:\vcpkg*") {
    $currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (!$currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host "Please run this script as administrator to set vcpkg environment variables."
        Exit 1
    }

    Write-Host "`nAdding vcpkg to the system environment variables..."

    try {
        [Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "Machine")
    } catch {
        Write-Host "Failed to set VCPKG_ROOT environment variable."
        Exit 1
    }

    try {
        [Environment]::SetEnvironmentVariable("PATH", "$envPath;C:\vcpkg", "Machine")
    } catch {
        Write-Host "Failed to add vcpkg directory to the system PATH env."
        Exit 1
    }

    $env:PATH += ";C:\vcpkg"
    Write-Host "Done. Please restart the terminal to use vcpkg command."
}

try {
    vcpkg integrate install
} catch {
    Write-Host "Failed to integrate vcpkg user-wide."
    Exit 1
}

Exit 0