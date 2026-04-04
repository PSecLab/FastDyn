from __future__ import annotations

import asyncio
import json
import os
from typing import Any, Dict, List

from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client
from openai import OpenAI


MODEL = "gpt-4.1-mini"

# URL used by the OpenAI client; can be overridden with OPENAI_URL.
OPENAI_URL = os.environ.get("OPENAI_URL", "https://api.openai.com/v1/chat/completions")


def _build_openai_client() -> OpenAI:
    """
    Create an OpenAI client using the OPENAI_API_KEY environment variable.
    """
    # OpenAI's Python SDK will read OPENAI_API_KEY automatically.
    # We also allow overriding the base URL via OPENAI_URL so this CLI,
    # the MCP server, and the Rust shim all use the same endpoint.
    client = OpenAI(base_url=OPENAI_URL.rsplit("/chat/completions", 1)[0])
    return client


async def chat_with_mcp() -> None:
    """
    Simple CLI chat loop that:
    - Starts the local MCP server (mcp_server.py) over stdio.
    - Lists available tools from the server.
    - Lets the model (via OpenAI) decide when to call tools.
    """

    client = _build_openai_client()

    # Describe how to launch the MCP server (mcp_server.py) as a child process.
    server = StdioServerParameters(
        command="python",
        args=["mcp_server.py"],
    )

    # Connect to the MCP server over stdio.
    async with stdio_client(server) as streams:
        async with ClientSession(*streams) as session:
            # Perform MCP initialization handshake before sending requests.
            await session.initialize()

            tools_result = await session.list_tools()
            tools = tools_result.tools
            print("Connected tools:", [t.name for t in tools])

            messages: List[Dict[str, Any]] = []

            # Build OpenAI tool schemas from MCP tools.
            openai_tools: List[Dict[str, Any]] = []
            for t in tools:
                params_schema = t.inputSchema or {
                    "type": "object",
                    "properties": {},
                    "additionalProperties": False,
                }
                openai_tools.append(
                    {
                        "type": "function",
                        "function": {
                            "name": t.name,
                            "description": t.description or "",
                            "parameters": params_schema,
                        },
                    }
                )

            while True:
                try:
                    user = input("\nYou: ").strip()
                except (EOFError, KeyboardInterrupt):
                    print("\nExiting.")
                    break

                if not user or user.lower() in {"exit", "quit"}:
                    print("Goodbye.")
                    break

                messages.append({"role": "user", "content": user})

                # First call: allow the model to decide whether to call a tool.
                response = client.chat.completions.create(
                    model=MODEL,
                    messages=messages,
                    tools=openai_tools,
                    tool_choice="auto",
                )

                msg = response.choices[0].message

                # If the model requested a tool call, execute it via the MCP client.
                if msg.tool_calls:
                    for call in msg.tool_calls:
                        # call.function.arguments is a JSON string
                        try:
                            args = json.loads(call.function.arguments or "{}")
                        except json.JSONDecodeError:
                            args = {}

                        result = await session.call_tool(
                            call.function.name,
                            args,
                        )

                        # Record the assistant's tool call and the tool result.
                        messages.append(
                            {
                                "role": "assistant",
                                "content": msg.content or "",
                                "tool_calls": [
                                    {
                                        "id": call.id,
                                        "type": call.type,
                                        "function": {
                                            "name": call.function.name,
                                            "arguments": call.function.arguments,
                                        },
                                    }
                                ],
                            }
                        )

                        messages.append(
                            {
                                "role": "tool",
                                "tool_call_id": call.id,
                                "content": json.dumps(
                                    [c.model_dump() for c in result.content]
                                ),
                            }
                        )

                    # Second call: give the model the tool results so it can respond.
                    followup = client.chat.completions.create(
                        model=MODEL,
                        messages=messages,
                    )
                    final_msg = followup.choices[0].message
                    print("\nLLM:", final_msg.content)
                    messages.append(
                        {
                            "role": "assistant",
                            "content": final_msg.content,
                        }
                    )
                    continue

                # No tools used: just show the model's response and continue.
                if msg.content and msg.content.strip():
                    print("\nLLM:", msg.content)
                    messages.append(
                        {
                            "role": "assistant",
                            "content": msg.content,
                        }
                    )
                else:
                    print(
                        "\nLLM did not call any tools and did not provide an answer. "
                        "Try rephrasing or giving more detail."
                    )


if __name__ == "__main__":
    try:
        asyncio.run(chat_with_mcp())
    except ValueError as e:
        print(f"Value Error: {e}")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        print("Exiting.")
