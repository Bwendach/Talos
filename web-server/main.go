package main

import (
	"context"
	"encoding/json"
	"log"
	"os"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
	mqtt "github.com/mochi-mqtt/server/v2"
	"github.com/mochi-mqtt/server/v2/hooks/auth"
	"github.com/mochi-mqtt/server/v2/listeners"
	"github.com/mochi-mqtt/server/v2/packets"
)

type Reading struct {
	Ts          *int64   `json:"ts"`
	UptimeMs    int64    `json:"uptime_ms"`
	Temperature *float64 `json:"temperature"`
	Humidity    *float64 `json:"humidity"`
	MicPeak     int64    `json:"mic_peak"`
	IrCmd       *string  `json:"ir_cmd"`
}

const createTable = `
CREATE TABLE IF NOT EXISTS readings (
    ts          TIMESTAMPTZ,
    uptime_ms   BIGINT NOT NULL,
    temperature REAL,
    humidity    REAL,
    mic_peak    INTEGER NOT NULL,
    ir_cmd      TEXT,
    received_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (ts, uptime_ms)
)`

const insertReading = `
INSERT INTO readings (ts, uptime_ms, temperature, humidity, mic_peak, ir_cmd)
VALUES ($1, $2, $3, $4, $5, $6)
ON CONFLICT DO NOTHING`

func main() {
	ctx := context.Background()

	db, err := pgxpool.New(ctx, os.Getenv("DATABASE_URL"))
	if err != nil {
		log.Fatal(err)
	}

	if _, err := db.Exec(ctx, createTable); err != nil {
		log.Fatal(err)
	}

	server := mqtt.New(&mqtt.Options{InlineClient: true})

	if err := server.AddHook(new(auth.AllowHook), nil); err != nil {
		log.Fatal(err)
	}

	if err := server.AddListener(listeners.NewTCP(listeners.Config{ID: "tcp", Address: ":4092"})); err != nil {
		log.Fatal(err)
	}

	err = server.Subscribe("talos/readings", 1, func(cl *mqtt.Client, sub packets.Subscription, pk packets.Packet) {
		var r Reading

		if err := json.Unmarshal(pk.Payload, &r); err != nil {
			log.Printf("bad payload %q: %v", pk.Payload, err)
			return
		}

		var ts *time.Time

		if r.Ts != nil {
			t := time.UnixMilli(*r.Ts)
			ts = &t
		}

		if _, err := db.Exec(ctx, insertReading, ts, r.UptimeMs, r.Temperature, r.Humidity, r.MicPeak, r.IrCmd); err != nil {
			log.Printf("insert: %v", err)
		}
	})

	if err != nil {
		log.Fatal(err)
	}

	err = server.Subscribe("talos/status", 2, func(cl *mqtt.Client, sub packets.Subscription, pk packets.Packet) {
		log.Printf("board is %s", pk.Payload)
	})

	if err != nil {
		log.Fatal(err)
	}

	if err := server.Serve(); err != nil {
		log.Fatal(err)
	}

	select {}
}
