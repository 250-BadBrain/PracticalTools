//go:build windows

package main

import (
	"os/exec"
	"syscall"
)

func prepareCommand(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
}

func processRunning(pid string) bool {
	cmd := exec.Command("tasklist", "/fi", "PID eq "+pid)
	prepareCommand(cmd)
	out, err := cmd.Output()
	if err != nil {
		return false
	}
	return containsPID(string(out), pid)
}
