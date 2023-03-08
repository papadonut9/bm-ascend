FROM ubuntu:22.04.2

# Install required packages
RUN apt-get update && \
    apt-get install -y build-essential

# Copy application files
COPY . /app

# Set working directory
WORKDIR /app

# Build the application
RUN make

# Set command to run when the container starts
CMD ["./build/ascend"]
