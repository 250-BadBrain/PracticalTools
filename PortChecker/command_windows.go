//go:build windows

package main

import (
	"os/exec"
	"syscall"
)

func commandOutput(name string, args ...string) ([]byte, error) {
	cmd := hiddenCommand(name, args...)
	return cmd.Output()
}

func commandCombinedOutput(name string, args ...string) ([]byte, error) {
	cmd := hiddenCommand(name, args...)
	return cmd.CombinedOutput()
}

func hiddenCommand(name string, args ...string) *exec.Cmd {
	cmd := exec.Command(name, args...)
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
	return cmd
}
