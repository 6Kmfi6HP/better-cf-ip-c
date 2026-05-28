# install.ps1 — Install the latest better-cf-ip-c binary on Windows
#
# Usage:
#   iwr -Uri https://github.com/6Kmfi6HP/better-cf-ip-c/releases/latest/download/install.ps1 | iex
#   powershell -c "irm https://github.com/6Kmfi6HP/better-cf-ip-c/releases/latest/download/install.ps1 | iex"
#
# Options:
#   -Version <tag>   Install a specific version (e.g., v1.0.0)
#   -Prefix <path>   Install to a specific directory (default: $env:USERPROFILE\.local\bin)

param(
  [string]$Version = "",
  [string]$Prefix = ""
)

$Repo = "6Kmfi6HP/better-cf-ip-c"
$BinaryName = "better-cf-ip-c.exe"
$Binary = "better-cf-ip-c-windows-x86_64.exe"

# --- Determine install prefix ---
if (-not $Prefix) {
  $Prefix = "$env:USERPROFILE\.local\bin"
}

# --- Determine version ---
if (-not $Version) {
  Write-Host "Fetching latest release..."
  try {
    $releases = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest"
    $Version = $releases.tag_name
  } catch {
    Write-Error "Could not determine latest version from GitHub API"
    exit 1
  }
}

# --- Create install directory ---
New-Item -ItemType Directory -Force -Path $Prefix | Out-Null

# --- Download ---
$DownloadUrl = "https://github.com/$Repo/releases/download/$Version/$Binary"
$OutPath = "$Prefix\$BinaryName"
Write-Host "Downloading better-cf-ip-c $Version ($Binary)..."
Invoke-WebRequest -Uri $DownloadUrl -OutFile $OutPath

Write-Host ""
Write-Host "Installed to $OutPath"
Write-Host ""
Write-Host "Make sure $Prefix is in your PATH:"
Write-Host "  `$env:Path = `"$Prefix;`$env:Path`""
Write-Host ""
Write-Host "For permanent addition, run:"
Write-Host "  [Environment]::SetEnvironmentVariable('Path', '$Prefix;' + [Environment]::GetEnvironmentVariable('Path', 'User'), 'User')"
Write-Host ""
Write-Host "Run:"
Write-Host "  $BinaryName --help"
