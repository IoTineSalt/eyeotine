#!/bin/bash
# Download latest models from https://github.com/ultralytics/yolov5/releases
# Usage:
#    $ bash weights/download_weights.sh

python - <<EOF
from yolov5.utils.google_utils import attempt_download
import pathlib
if not pathlib.Path('yolov5s.pt').exists():
  attempt_download(f'yolov5s.pt')
else:
  print("weights already downloaded")
EOF
