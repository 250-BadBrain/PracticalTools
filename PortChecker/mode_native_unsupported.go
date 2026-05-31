//go:build native && !windows && !linux

package main

import "errors"

func runDefault() error {
	return runGUI(nil)
}

func runGUI(args []string) error {
	return errors.New("native GUI currently supports Windows and Linux only")
}
