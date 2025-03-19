FROM ubuntu:22.04

LABEL maintainer="wjmiao@ucdavis.edu"

WORKDIR /root/

COPY build_llvm_env.sh /root/

RUN /root/build_llvm_env.sh