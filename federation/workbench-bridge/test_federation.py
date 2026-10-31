"""
Self-contained integration test suite for Dual-Xiaozhi MCP-over-WebSocket federation bridge.
Tests:
1. MCP Protocol Handshake (initialize, tools/list)
2. ask_peer_xiaozhi tool execution
3. sync_shared_memory tool execution with SQLite persistence verification
4. agent_dialogue_mode with 16kHz PCM audio turn generation
5. get_node_telemetry tool execution
"""

import os
import sys
import time
import uuid
import socket
import asyncio
import logging
import sqlite3
from typing import Generator

# Add current directory to path
sys.path.insert(0, os.path.dirname(__file__))

from xiaozhi_federation_mcp import (
    app,
    hub,
    XiaozhiFederationClient,
    FederationMemoryDB,
    DEFAULT_WS_PATH,
    SAMPLE_RATE_HZ,
    SAMPLES_PER_FRAME,
    BYTES_PER_FRAME
)
import uvicorn

logging.basicConfig(level=logging.INFO, format="[%(levelname)s] %(message)s")
logger = logging.getLogger("test_federation")


def get_ephemeral_port() -> int:
    """Finds an available TCP port for isolated testing."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


async def run_integration_tests():
    test_port = get_ephemeral_port()
    test_db = os.path.join(os.path.dirname(__file__), f"test_memory_{uuid.uuid4().hex[:6]}.db")
    
    # Configure test hub instance
    hub.node_id = "xiaozhi-workbench-test"
    hub.db = FederationMemoryDB(test_db)

    # Start Uvicorn server in background task
    config = uvicorn.Config(app=app, host="127.0.0.1", port=test_port, log_level="warning")
    server = uvicorn.Server(config)
    
    server_task = asyncio.create_task(server.serve())
    
    # Wait for server ready
    connected = False
    for attempt in range(20):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                if s.connect_ex(("127.0.0.1", test_port)) == 0:
                    connected = True
                    break
        except Exception:
            pass
        await asyncio.sleep(0.1)

    assert connected, f"Test server failed to start on port {test_port}"
    logger.info(f"Test server running on port {test_port}")

    passed_tests = 0
    total_tests = 8
    received_notifications = []

    def on_notification_received(payload):
        received_notifications.append(payload)

    try:
        # Initialize client representing xiaozhi-pi with notification callback
        client = XiaozhiFederationClient(
            node_id="xiaozhi-pi",
            peer_host="127.0.0.1",
            peer_port=test_port,
            on_notification=on_notification_received
        )
        await client.connect(timeout=5.0)
        assert client.is_connected, "Client failed to connect to federation WebSocket"

        # Test 1: MCP Handshake & Tool Listing
        logger.info("Test 1: MCP Protocol Handshake & Tool Discovery...")
        tools_res = await client.send_rpc("tools/list")
        tools = tools_res.get("result", {}).get("tools", [])
        tool_names = [t["name"] for t in tools]
        assert "ask_peer_xiaozhi" in tool_names, "Missing ask_peer_xiaozhi tool"
        assert "sync_shared_memory" in tool_names, "Missing sync_shared_memory tool"
        assert "agent_dialogue_mode" in tool_names, "Missing agent_dialogue_mode tool"
        logger.info(f"Test 1 PASSED: Discovered tools {tool_names}")
        passed_tests += 1

        # Test 2: ask_peer_xiaozhi (Client to Server: Pi -> Workbench)
        logger.info("Test 2: ask_peer_xiaozhi (Pi -> Workbench) execution...")
        ask_result = await client.ask_peer_xiaozhi(
            target_node="xiaozhi-workbench-test",
            query="Analyze IMU and rover battery status",
            context={"battery_volts": 12.4, "imu_pitch": -1.2}
        )
        assert ask_result.get("status") == "success", f"ask_peer failed: {ask_result}"
        assert ask_result.get("answering_node") == "xiaozhi-workbench-test"
        assert "Analyze IMU and rover battery status" in ask_result.get("reply", "")
        # Cross-node compatibility check: verify both 'reply' and 'text' exist for xiaozhi_pi_agent.py interop
        assert ask_result.get("text") == ask_result.get("reply"), "Expected 'text' key to match 'reply'"
        logger.info(f"Test 2 PASSED: Peer responded with: {ask_result.get('reply')}")
        passed_tests += 1

        # Test 3: Bidirectional RPC (Server to Client: Workbench Hub -> Pi)
        logger.info("Test 3: Bidirectional RPC (Workbench Hub -> Pi client)...")
        server_to_peer_res = await hub.ask_peer(
            target_node="xiaozhi-pi",
            query="Query local Pi peripheral sensor health",
            context={"source": "master_hub"}
        )
        assert server_to_peer_res.get("status") == "success", f"Server-to-peer call failed: {server_to_peer_res}"
        assert server_to_peer_res.get("answering_node") == "xiaozhi-pi"
        assert "Query local Pi peripheral sensor health" in server_to_peer_res.get("reply", "")
        logger.info(f"Test 3 PASSED: Hub successfully queried client peer: {server_to_peer_res.get('reply')}")
        passed_tests += 1

        # Test 4: sync_shared_memory and SQLite verification
        logger.info("Test 4: sync_shared_memory & SQLite persistence...")
        test_session_id = f"sess_{uuid.uuid4().hex[:8]}"
        sync_result = await client.sync_shared_memory(
            session_id=test_session_id,
            summary="Emergency RNDIS fallback engaged after Wi-Fi timeout",
            tags=["network", "rndis", "emergency", "s3"],
            metadata={"ip": "192.168.0.106", "rssi": -85}
        )
        assert sync_result.get("status") == "success", f"sync failed: {sync_result}"
        assert sync_result.get("session_id") == test_session_id

        # Verify SQLite record directly
        stored_rows = hub.db.query_memory(session_id=test_session_id)
        assert len(stored_rows) == 1, f"Expected 1 record in SQLite, got {len(stored_rows)}"
        assert stored_rows[0]["summary"] == "Emergency RNDIS fallback engaged after Wi-Fi timeout"
        assert "rndis" in stored_rows[0]["tags"]
        assert stored_rows[0]["source_node"] == "xiaozhi-pi"
        logger.info(f"Test 4 PASSED: Verified SQLite persistence (row ID {sync_result.get('persisted_row_id')})")
        passed_tests += 1

        # Test 5: agent_dialogue_mode with 16kHz audio turns and WebSocket broadcasting
        logger.info("Test 5: agent_dialogue_mode & 16kHz PCM audio generation & broadcast...")
        dialogue_result = await client.agent_dialogue_mode(
            topic="Autonomous Rover Navigation at Desk",
            max_turns=4
        )
        assert dialogue_result.get("status") == "success", f"dialogue failed: {dialogue_result}"
        assert dialogue_result.get("turns_completed") == 4
        turns = dialogue_result.get("turns", [])
        assert len(turns) == 4
        import base64
        for turn in turns:
            assert turn["audio_frame_bytes"] == BYTES_PER_FRAME
            assert turn["sample_rate"] == SAMPLE_RATE_HZ
            assert turn["samples_count"] == SAMPLES_PER_FRAME
            assert "audio_base64" in turn
            raw_audio = base64.b64decode(turn["audio_base64"])
            assert len(raw_audio) == BYTES_PER_FRAME

        # Verify turns stored in SQLite
        history = hub.db.get_dialogue_history(dialogue_result["session_id"])
        assert len(history) == 4, f"Expected 4 history turns in SQLite, found {len(history)}"

        # Verify WebSocket broadcast notifications arrived at client
        assert len(received_notifications) >= 4, f"Expected 4 broadcast turns, received {len(received_notifications)}"
        notif_audio = base64.b64decode(received_notifications[0]["params"]["audio_base64"])
        assert len(notif_audio) == BYTES_PER_FRAME
        logger.info(f"Test 5 PASSED: 4-turn dialogue generated, persisted, and broadcast over WebSocket ({BYTES_PER_FRAME}B frames)")
        passed_tests += 1

        # Test 6: Binary audio frame streaming resilience
        logger.info("Test 6: Raw binary audio frame ingestion...")
        dummy_pcm_frame = b"\x00\x00" * SAMPLES_PER_FRAME
        await client.send_audio_frame(dummy_pcm_frame)
        await asyncio.sleep(0.05)
        assert client.is_connected, "Client connection disconnected on binary audio frame"
        logger.info("Test 6 PASSED: Ingested raw binary PCM frame without protocol disruption")
        passed_tests += 1

        # Test 7: get_node_telemetry
        logger.info("Test 7: Node telemetry tool execution...")
        telem_res = await client.send_rpc("tools/call", {
            "name": "get_node_telemetry",
            "arguments": {"node_id": "xiaozhi-workbench-test"}
        })
        import json
        content_text = telem_res.get("result", {}).get("content", [{}])[0].get("text", "{}")
        telem_data = json.loads(content_text)
        assert "cpu_percent" in telem_data
        assert "ram_used_mb" in telem_data
        logger.info(f"Test 7 PASSED: Telemetry read successfully: CPU={telem_data['cpu_percent']}%, RAM={telem_data['ram_used_mb']}MB")
        passed_tests += 1

        await client.close()

    finally:
        # Stop Uvicorn server cleanly
        server.should_exit = True
        await server_task
        # Test 8: Clean shutdown & database file cleanup without file lock leaks
        if os.path.exists(test_db):
            try:
                os.remove(test_db)
                assert not os.path.exists(test_db), "Test database file still exists after removal"
                logger.info("Test 8 PASSED: Database cleanly unlinked with zero connection/handle leaks")
                passed_tests += 1
            except Exception as e:
                logger.error(f"Failed to remove test DB (connection leak): {e}")

    logger.info(f"\n==========================================")
    logger.info(f"ALL TESTS COMPLETED: {passed_tests}/{total_tests} PASSED")
    logger.info(f"==========================================")
    return passed_tests == total_tests


if __name__ == "__main__":
    success = asyncio.run(run_integration_tests())
    sys.exit(0 if success else 1)
