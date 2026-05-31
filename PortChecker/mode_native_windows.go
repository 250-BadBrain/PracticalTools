//go:build native && windows

package main

import "errors"

func runDefault() error {
	return runNativeGUI(nil)
}

func runGUI(args []string) error {
	if len(args) > 0 {
		return errors.New("native Windows GUI does not support command arguments yet")
	}
	return runNativeGUI(args)
}
