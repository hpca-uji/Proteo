#ifndef MAM_SPAWN_PORTSERVICE_H
#define MAM_SPAWN_PORTSERVICE_H
#include <mpi.h>
#include "Spawn_DataStructure.h"

#define MAM_SERVICE_UNNEEDED -1  // Constant to avoid opening a service if not required

void init_ports(Spawn_ports *spawn_port);
void open_port(Spawn_ports *spawn_port, int open_port, int open_service);
void close_port(Spawn_ports *spawn_port);
void discover_remote_port(int id_group, Spawn_ports *spawn_port);
void free_ports(Spawn_ports *spawn_port);
#endif
