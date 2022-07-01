FROM fuzzers/libfuzzer:12.0

RUN apt-get update
RUN apt install -y build-essential wget git clang cmake  automake autotools-dev  libtool libjpeg-dev libx11-dev
RUN git clone https://github.com/morganstanley/binlog.git
WORKDIR /binlog
RUN cmake .
RUN make
RUN make install
COPY fuzzers/fuzz_binlog.cpp .
RUN clang++ -g -O1 -I/usr/local/include/ -fsanitize=fuzzer,address fuzz_binlog.cpp -o /binlogFuzz  libbinlog.a

ENTRYPOINT []
CMD ["/binlogFuzz"]
