FROM corey/rust-source

RUN apt-get update && apt-get install -y \
  imagemagick

WORKDIR /rustbeltrust

COPY . .

WORKDIR /rustbeltrust/exercise2/jpeg-decoder
RUN cargo build --release

WORKDIR /rustbeltrust/exercise3/rust-url
RUN cargo build --release

WORKDIR /rustbeltrust/
