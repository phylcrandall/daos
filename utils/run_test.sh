#!/bin/bash
# A Script that runs a series of simple tests that
#  can be executed in the Jenkins environment.
#  The flock is to ensure that only one jenkins builder
#  runs the tests at a time, as the tests use the same
#  mount point.  For now, there actually isn't much
#  contention, however, because /mnt/daos on the jenkins nodes
#  is subdivided into /mnt/daos/el7; /mnt/daos/sles12sp3;
#  /mnt/daos/ubuntu1404; and /mnt/daos/ubuntu1604
#  These are mapped into /mnt/daos in the docker containers
#  for the corresponding Jenkins distro builder. This "magic"
#  is configured in the Jenkins config page for daos builders
#  in the "build" section.
#
#  Note: Uses .build_vars.sh to find daos artifacts
#  Note: New tests should return non-zero if there are any
#    failures.

#check for existence of /mnt/daos first:
failed=0

if [ -d /work ]; then
    export D_LOG_FILE=/work/daos.log
fi

run_test()
{
    # We use flock as a way of locking /mnt/daos so multiple runs can't hit it
    #     at the same time.
    # We use grep to filter out any potential "SUCCESS! NO TEST FAILURES"
    #    messages as daos_post_build.sh will look for this and mark the tests
    #    as passed, which we don't want as we need to check all of the tests
    #    before deciding this. Also, we intentionally leave off the last 'S'
    #    in that error message so that we don't guarantee printing that in
    #    every run's output, thereby making all tests here always pass.
    if ! time "$@"; then
        echo "Test $* failed with exit status ${PIPESTATUS[0]}."
        ((failed = failed + 1))
    fi
}

if [ "$1" = "--init" ]; then
    INIT=true
else
    INIT=false
fi

if $INIT; then
    sudo bash -c 'echo "1" > /proc/sys/kernel/sysrq'
    if grep /mnt/daos\  /proc/mounts; then
        sudo umount /mnt/daos
    else
        if [ ! -d /mnt/daos ]; then
            sudo mkdir -p /mnt/daos
        fi
    fi
    sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
    df -h /mnt/daos
    trap 'sudo umount /mnt/daos' EXIT
fi

if [ -d "/mnt/daos" ]; then
    # shellcheck disable=SC1091
    source ./.build_vars.sh
    # fix up paths so they are relative to $PWD since we might not
    # be in the same path as the software was built
    SL_PREFIX=$PWD/${SL_PREFIX/*\/install/install}
    SL_OMPI_PREFIX=$PWD/${SL_OMPI_PREFIX/*\/install/install}
    run_test "${SL_PREFIX}/bin/vos_tests" -A 500
    run_test "${SL_PREFIX}/bin/vos_tests" -n -A 500
    run_test src/common/tests/btree.sh ukey -s 20000
    run_test src/common/tests/btree.sh direct -s 20000
    run_test src/common/tests/btree.sh -s 20000
    run_test src/common/tests/btree.sh perf -s 20000
    run_test src/common/tests/btree.sh perf direct -s 20000
    run_test src/common/tests/btree.sh perf ukey -s 20000
    run_test build/src/common/tests/sched
    run_test build/src/common/tests/drpc_tests
    run_test build/src/client/api/tests/eq_tests
    run_test src/vos/tests/evt_ctl.sh
    run_test build/src/vos/vea/tests/vea_ut
    run_test src/rdb/raft_tests/raft_tests.py
    # Environment variables specific to the rdb tests
    export PATH=$SL_PREFIX/bin:$PATH
    export OFI_INTERFACE=lo
    # Satisfy CGO Link requirements for go-spdk binding imports
    export LD_LIBRARY_PATH=$SL_PREFIX/lib:${LD_LIBRARY_PATH}
    run_test src/rdb/tests/rdb_test_runner.py "${SL_OMPI_PREFIX}"
    run_test src/control/run_go_tests.sh
    run_test build/src/security/tests/cli_security_tests

    if [ $failed -eq 0 ]; then
        # spit out the magic string that the post build script looks for
        echo "SUCCESS! NO TEST FAILURES"
    else
        echo "FAILURE: $failed tests failed"
        exit 1
    fi
else
    echo "/mnt/daos isn't present for unit tests"
fi
