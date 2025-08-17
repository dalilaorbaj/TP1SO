FROM agodio/itba-so-multi-platform:3.0

WORKDIR /TP1SO

COPY . .

RUN make chompchamps && chmod u-s build/ChompChamps

ENTRYPOINT ["./tools/run-chompchamps.sh"]
CMD []