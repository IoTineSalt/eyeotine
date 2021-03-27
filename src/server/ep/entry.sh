#!/usr/bin/env bash
pip install -r "$1/requirements.txt"
cd "$1"
python main.py
