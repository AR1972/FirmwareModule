@ECHO OFF
RMDIR /Q /S "%~dp0Release"
RMDIR /Q /S "%~dp0ipch"
RMDIR /Q /S "%~dp0A1902AB9-5394-45F2-857A-12824213EEFB\Release"
RMDIR /Q /S "%~dp0LicenseData\Release"
DEL "%~dp0FirmwareModule.sdf"
