# Spaghetti - The C2 Framework
This is a C2 framework I built from scratch, that targets Windows machines and is currently built to export outputs of a few commands.

## Deployment: Server

1. Clone the GitHub repository: `git clone https://github.com/thesrikarpaida/Spaghetti-The-C2-Framework.git`
2. Then, `cd server`
3. Assuming you already have Python3 installed, install the following dependencies: `python3 -m pip install fastapi uvicorn jinja2`
4. Run the server: `python3 server.py`
5. The server will be accessible at `127.0.0.1:8000`

## Deployment: Agent

The Spaghetti_Agent.exe is already a compiled, executable version of the C agent file. It can directly be deployed to the target machine instead of compiling the C file again in case you do not wish to make any changes.

If you want to edit and compile a fresh version of the agent, then:
 - Install CMake.
 - Make sure the CMakesList.txt and agent.c files are in the same folder.
 - Install a C compiler like MinGW.
 - Create a build directory in the same folder as the above files.
     + `mkdir build`
     + `cd build`
 - Generate the build files with CMake.
     + `cmake ..`
 - Build the program.
     + `cmake --build .`
 - Deploy it to target machine and execute it, making sure the server is already running so it can connect to it.
