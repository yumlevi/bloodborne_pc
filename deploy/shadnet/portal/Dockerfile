FROM python:3.12-slim
ENV PYTHONUNBUFFERED=1
WORKDIR /app
COPY server.py /app/server.py
RUN groupadd --gid 10001 portal && useradd --uid 10001 --gid 10001 --no-create-home portal \
    && mkdir -p /var/lib/portal && chown portal:portal /var/lib/portal
USER portal
EXPOSE 18080/tcp
CMD ["python", "/app/server.py"]
