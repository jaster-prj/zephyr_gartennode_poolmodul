FROM python:3.13-slim

WORKDIR /app
COPY testbed/companion/requirements.txt ./requirements.txt
RUN pip install --no-cache-dir -r requirements.txt
COPY testbed/companion/ ./

CMD ["python", "companion.py"]
