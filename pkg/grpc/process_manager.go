// SPDX-License-Identifier: Apache-2.0
// Copyright Authors of Tetragon

package grpc

import (
	"context"
	"sync"

	"github.com/cilium/tetragon/api/v1/tetragon"
	"github.com/cilium/tetragon/pkg/eventcache"
	"github.com/cilium/tetragon/pkg/logger"
	"github.com/cilium/tetragon/pkg/metrics/eventmetrics"
	"github.com/cilium/tetragon/pkg/option"
	"github.com/cilium/tetragon/pkg/reader/node"
	"github.com/cilium/tetragon/pkg/reader/notify"
	"github.com/cilium/tetragon/pkg/rthooks"
	"github.com/cilium/tetragon/pkg/sensors"
	"github.com/cilium/tetragon/pkg/server"
)

// ProcessManager maintains a cache of processes from tetragon exec events.
type ProcessManager struct {
	Server *server.Server
	// synchronize access to the listeners map.
	mux       sync.Mutex
	listeners map[server.Listener]struct{}
}

// NewProcessManager returns a pointer to an initialized ProcessManager struct.
func NewProcessManager(
	ctx context.Context,
	wg *sync.WaitGroup,
	manager *sensors.Manager,
	hookRunner *rthooks.Runner,
) (*ProcessManager, error) {
	pm := &ProcessManager{
		listeners: make(map[server.Listener]struct{}),
	}

	pm.Server = server.NewServer(ctx, wg, pm, manager, hookRunner)

	// Exec cache is always needed to ensure events have an associated Process{}
	eventcache.New(pm)

	logger.GetLogger().Info("Starting process manager",
		"enableK8s", option.Config.EnableK8s,
		"enableProcessCred", option.Config.EnableProcessCred,
		"enableProcessNs", option.Config.EnableProcessNs)
	return pm, nil
}

// Notify implements Listener.Notify.
func (pm *ProcessManager) Notify(event notify.Message) error {
	processedEvent := event.HandleMessage()
	if processedEvent != nil {
		pm.NotifyListener(event, processedEvent)
	}
	return nil
}

// Close implements Listener.Close.
func (pm *ProcessManager) Close() error {
	return nil
}

func (pm *ProcessManager) AddListener(listener server.Listener) {
	logger.GetLogger().Debug("Adding a getEventsListener", "getEventsListener", listener)
	pm.mux.Lock()
	defer pm.mux.Unlock()
	pm.listeners[listener] = struct{}{}
}

func (pm *ProcessManager) RemoveListener(listener server.Listener) {
	logger.GetLogger().Debug("Removing a getEventsListener", "getEventsListener", listener)
	pm.mux.Lock()
	defer pm.mux.Unlock()
	delete(pm.listeners, listener)
}

func (pm *ProcessManager) NotifyListener(original interface{}, processed *tetragon.GetEventsResponse) {
	pm.mux.Lock()
	defer pm.mux.Unlock()
	node.SetCommonFields(processed)
	for l := range pm.listeners {
		l.Notify(processed)
	}
	eventmetrics.ProcessEvent(original, processed)
}
