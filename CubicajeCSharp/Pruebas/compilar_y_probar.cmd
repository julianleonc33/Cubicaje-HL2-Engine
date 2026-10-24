@echo off
rem Compila el motor C# + las pruebas de consola con el csc.exe de Visual Studio / Build Tools
rem y ejecuta las pruebas. Uso:  compilar_y_probar.cmd [segundos para el caso 205]
setlocal
set HERE=%~dp0
set CSC=
for %%P in (
  "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\Roslyn\csc.exe"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\Roslyn\csc.exe"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\Roslyn\csc.exe"
  "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\Roslyn\csc.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\Roslyn\csc.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe"
) do if not defined CSC if exist %%P set CSC=%%P
if not defined CSC (
  echo No se encontro csc.exe. Alternativa: dotnet run --project "%HERE%PruebasCubicaje.csproj"
  exit /b 1
)
if not exist "%HERE%bin" mkdir "%HERE%bin"
%CSC% -nologo -langversion:7.3 -codepage:65001 -optimize+ -out:"%HERE%bin\PruebasCubicaje.exe" "%HERE%..\Runtime\*.cs" "%HERE%PruebasCubicaje.cs"
if errorlevel 1 exit /b 1
"%HERE%bin\PruebasCubicaje.exe" %*
