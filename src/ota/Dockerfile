FROM python
WORKDIR .
COPY requirements.txt requirements.txt
RUN pip3 install --upgrade pip setuptools \
 && pip install --upgrade pip
RUN pip install --no-cache-dir  -r requirements.txt
EXPOSE 80
COPY . .
CMD ["python", "ota_webserver.py"]
