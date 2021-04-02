FROM python
WORKDIR .
COPY requirements.txt requirements.txt
RUN apt-get update && apt install libgl1-mesa-glx -y
RUN pip3 install --upgrade pip setuptools \
 && pip install --upgrade pip
RUN pip install -r requirements.txt
EXPOSE 2021
COPY . .
RUN ["sh", "download_weights.sh"]
CMD ["python", "angle_prediction.py"]
