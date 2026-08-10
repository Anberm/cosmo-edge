#!/bin/bash
set -euo pipefail

PROJECT_ROOT_PATH=$(cd "$(dirname "$0")"; pwd)/../..

exec python3 "${PROJECT_ROOT_PATH}/tools/agent_workflow.py" doctor "$@"
