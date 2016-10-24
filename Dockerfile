# FROM corey/rust-source

FROM ubuntu:14.04

RUN apt-get update && apt-get install -y \
  curl \
  file \
  g++ \
  g++-multilib \
  libstdc++-4.8-dev \
  llvm-3.8

RUN apt-get update && apt-get install -y \
  imagemagick

RUN curl -sSf https://static.rust-lang.org/rustup.sh | sh -s -- --channel=nightly --date=2016-07-30 --disable-sudo

WORKDIR /opt/afl.rs

ADD . .

WORKDIR /rustbeltrust

COPY . .

WORKDIR /rustbeltrust/exercise2/jpeg-decoder
RUN cargo build --release

WORKDIR /rustbeltrust/exercise3/rust-url
RUN cargo build --release

WORKDIR /rustbeltrust/exercise4/driver
RUN cargo build --release

WORKDIR /rustbeltrust/
