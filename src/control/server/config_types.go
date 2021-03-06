// (C) Copyright 2018 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"

	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/utils/handlers"
)

// Format represents enum specifying formatting behaviour
type Format string

const (
	// SAFE state defines cautionary behaviour where formatted devices will not be overwritten.
	SAFE Format = "safe"
	// CONTINUE state defines ehaviour which results in reuse of formatted devices.
	CONTINUE Format = "continue"
	// FORCE state defines aggressive/potentially resulting data loss formatting behaviour.
	FORCE Format = "force"

	// todo: implement Provider discriminated union
	// todo: implement LogMask discriminated union
)

//Server defines configuration options for DAOS IO Server instances
type server struct {
	// Rank parsed as string to allow zero value
	Rank            string   `yaml:"rank"`
	Cpus            []string `yaml:"cpus"`
	FabricIface     string   `yaml:"fabric_iface"`
	FabricIfacePort int      `yaml:"fabric_iface_port"`
	LogMask         string   `yaml:"log_mask"`
	LogFile         string   `yaml:"log_file"`
	EnvVars         []string `yaml:"env_vars"`
	ScmMount        string   `yaml:"scm_mount"`
	BdevClass       BdClass  `yaml:"bdev_class"`
	BdevList        []string `yaml:"bdev_list"`
	BdevNumber      int      `yaml:"bdev_number"`
	BdevSize        int      `yaml:"bdev_size"`
	// ioParams represents commandline options and environment variables
	// to be passed on I/O server invocation.
	CliOpts []string // tuples (short option, value) e.g. ["-p", "10000"...]
}

// UnmarshalYAML implements yaml.Unmarshaler on BdClass struct
func (b *BdClass) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var class string
	if err := unmarshal(&class); err != nil {
		return err
	}
	bdevClass := BdClass(class)
	switch bdevClass {
	case NVME, MALLOC, KDEV, FILE:
		*b = bdevClass
	default:
		return fmt.Errorf(
			"bdev_class value %v not supported in config (nvme/malloc/kdev/file)",
			bdevClass)
	}
	return nil
}

// todo: implement UnMarshal for LogMask discriminated union

// NewDefaultServer creates a new instance of server struct
// populated with defaults.
func NewDefaultServer() server {
	return server{
		BdevClass: NVME,
	}
}

// External interface provides methods to support various os operations.
type External interface {
	getenv(string) string
	runCommand(string) error
	writeToFile(string, string) error
	createEmpty(string, int64) error
}

type ext struct{}

// runCommand executes command in subshell (to allow redirection) and returns
// error result.
func (e *ext) runCommand(cmd string) error {
	return exec.Command("sh", "-c", cmd).Run()
}

// getEnv wraps around os.GetEnv and implements External.getEnv().
func (e *ext) getenv(key string) string {
	return os.Getenv(key)
}

// writeToFile wraps around handlers.WriteString and writes input
// string to given file pathk.
func (e *ext) writeToFile(in string, outPath string) error {
	return handlers.WriteString(outPath, in)
}

// createEmpty creates a file (if it doesn't exist) of specified size in bytes
// at the given path.
// If Fallocate not supported by kernel or backing fs, fall back to Truncate.
func (e *ext) createEmpty(path string, size int64) (err error) {
	if !filepath.IsAbs(path) {
		return fmt.Errorf("please specify absolute path (%s)", path)
	}
	if _, err = os.Stat(path); !os.IsNotExist(err) {
		return
	}
	file, err := handlers.OpenNewFile(path)
	if err != nil {
		return
	}
	defer file.Close()
	err = syscall.Fallocate(int(file.Fd()), 0, 0, size)
	if err != nil {
		e, ok := err.(syscall.Errno)
		if ok && (e == syscall.ENOSYS || e == syscall.EOPNOTSUPP) {
			fmt.Print(
				"Warning: Fallocate not supported, attempting Truncate: ", e)
			err = file.Truncate(size)
		}
	}
	return
}

type configuration struct {
	SystemName   string   `yaml:"name"`
	Servers      []server `yaml:"servers"`
	Provider     string   `yaml:"provider"`
	SocketDir    string   `yaml:"socket_dir"`
	Auto         bool     `yaml:"auto"`
	Format       Format   `yaml:"format"`
	AccessPoints []string `yaml:"access_points"`
	Port         int      `yaml:"port"`
	CaCert       string   `yaml:"ca_cert"`
	Cert         string   `yaml:"cert"`
	Key          string   `yaml:"key"`
	FaultPath    string   `yaml:"fault_path"`
	FaultCb      string   `yaml:"fault_cb"`
	FabricIfaces []string `yaml:"fabric_ifaces"`
	ScmMountPath string   `yaml:"scm_mount_path"`
	BdevInclude  []string `yaml:"bdev_include"`
	BdevExclude  []string `yaml:"bdev_exclude"`
	Hyperthreads bool     `yaml:"hyperthreads"`
	// development (subject to change) config fields
	Modules   string
	Attach    string
	SystemMap string
	Path      string
	ext       External
}

// UnmarshalYAML implements yaml.Unmarshaler on Format struct
func (f *Format) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var fm string
	if err := unmarshal(&fm); err != nil {
		return err
	}
	format := Format(fm)
	switch format {
	case SAFE, CONTINUE, FORCE:
		*f = format
	default:
		return fmt.Errorf(
			"format value %v not supported in config (safe/continue/force)",
			format)
	}
	return nil
}

// todo: implement UnMarshal for Provider discriminated union

// Parse decodes YAML representation of configure struct and checks for Group
func (c *configuration) Parse(data []byte) error {
	return yaml.Unmarshal(data, c)
}

// checkMount verifies that the provided path or parent directory is listed as
// a distinct mountpoint in output of os mount command.
func (c *configuration) checkMount(path string) error {
	path = strings.TrimSpace(path)
	f := func(p string) error {
		return c.ext.runCommand(fmt.Sprintf("mount | grep ' %s '", p))
	}
	if err := f(path); err != nil {
		return f(filepath.Dir(path))
	}
	return nil
}

// NewDefaultConfiguration creates a new instance of configuration struct
// populated with defaults.
func NewDefaultConfiguration(ext External) configuration {
	return configuration{
		SystemName:   "daos_server",
		SocketDir:    "/var/run/daos_server",
		Auto:         true,
		Format:       SAFE,
		AccessPoints: []string{"localhost"},
		Port:         10000,
		Cert:         "./.daos/daos_server.crt",
		Key:          "./.daos/daos_server.key",
		ScmMountPath: "/mnt/daos",
		Hyperthreads: false,
		Path:         "etc/daos_server.yml",
		ext:          ext,
	}
}

// NewConfiguration creates a new instance of configuration struct
// populated with defaults and default external interface.
func NewConfiguration() configuration {
	return NewDefaultConfiguration(&ext{})
}
