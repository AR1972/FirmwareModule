@ECHO OFF
"%~dp0LicenseData.exe" 0
IF ERRORLEVEL 2 PAUSE & EXIT
"%~dp0LicenseData.exe" 1 ASUS_FLASH
"%~dp0LicenseData.exe" 2 "%~dp0ASUS.BIN"
"%~dp0asus.exe"
ECHO restart computer to finish activation.
PAUSE
EXIT