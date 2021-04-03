FROM python:3.9-slim
WORKDIR .
COPY requirements.txt requirements.txt
RUN apt-get update && apt install libgl1-mesa-glx -y
RUN pip3 install --upgrade pip setuptools \
 && pip install --upgrade pip
RUN pip install -r requirements.txt
EXPOSE 5006
COPY . .
CMD ["bokeh", "serve", "visualize.py", "--log-level", "info"]
