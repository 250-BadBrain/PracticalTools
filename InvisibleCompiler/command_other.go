//go:build !windows

package main

import "os/exec"

func prepareCommand(cmd *exec.Cmd) {}

func processRunning(pid string) bool {
	return exec.Command("kill", "-0", pid).Run() == nil
}
