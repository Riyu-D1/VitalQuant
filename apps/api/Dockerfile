FROM python:3.12-slim
WORKDIR /app
COPY pyproject.toml README.md ./
COPY src ./src
COPY supabase ./supabase
RUN pip install --no-cache-dir ".[ingest]"
CMD ["uvicorn", "vitalq.ingest.app:app", "--host", "0.0.0.0", "--port", "8000"]
