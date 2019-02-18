# Development with Devspaces

### Devspaces 

Manage your Devspaces https://www.devspaces.io/.

Read up-to-date documentation about cli installation and operation in https://www.devspaces.io/devspaces/help.

Here follows the main commands used in Devspaces cli.

|action   |Description                                                                                   |
|---------|----------------------------------------------------------------------------------------------|
|`devspaces --help`                    |Check the available command names.                               |
|`devspaces create [options]`          |Creates a DevSpace using your local DevSpaces configuration file |
|`devspaces start <devSpace>`          |Starts the DevSpace named \[devSpace\]                           |
|`devspaces bind <devSpace>`           |Syncs the DevSpace with the current directory                    |
|`devspaces info <devSpace> [options]` |Displays configuration info about the DevSpace.                  |

Use `devspaces --help` to know about updated commands.

#### Development flow

You should have Devspaces cli services started and logged to develop with Devspaces.
The following commands should be issued from **project directory**.

1 - Create Devspaces

```bash
$ cd devspaces
$ devspaces create
$ cd ../

```

2 - Start containers

```bash
devspaces start arangodb
```

3 - Start containers synchronization

```bash
devspaces bind arangodb
```

4 - Grab some container info

```bash
devspaces info arangodb
```

5 - Connect to development container

```bash
devspaces exec arangodb
```
6 - Clone 
```bash
git clone https://github.com/trilogy-group/arangodb
```
7 - Build

```bash
mkdir -p build && cd build/ && cmake .. && make 
```
8 - Run
```bash
cd .. && build/bin/arangod -c etc/relative/arangod.conf --server.endpoint tcp://0.0.0.0:8529 /tmp/database-dir 
```
