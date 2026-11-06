FROM golang:1.27.3-alpine AS builder

WORKDIR /src

COPY go.mod go.sum ./
RUN go mod download && go mod verify

COPY . .

RUN CGO_ENABLED=0 go build -trimpath -o /out/server .

FROM gcr.io/distroless/static-debian13:nonroot

COPY --from=builder /out/server /

USER nonroot:nonroot

EXPOSE 4092

ENTRYPOINT [ "/server" ]
