FROM python:3.9-slim
WORKDIR .
COPY requirements.txt requirements.txt
RUN pip install -r requirements.txt
EXPOSE 2021
COPY ./ep .
CMD ["python", "main.py", "-mh", "broker"]
