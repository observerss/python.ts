FROM node:14.15-buster

RUN apt -y update && apt -y install \
    python3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY package.json .
RUN npm install

COPY ./src ./src
COPY ./tests ./tests
COPY ./plugins ./plugins
COPY tsconfig.json .
COPY binding.gyp .

RUN npm run build
RUN npm run test

CMD ["/bin/bash"]