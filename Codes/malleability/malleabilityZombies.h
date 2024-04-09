#ifndef MALLEABILITY_ZOMBIES_H
#define MALLEABILITY_ZOMBIES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>
#include <signal.h>
#include "malleabilityDataStructures.h"

void MAM_Zombies_service_init();
int MAM_Zombies_service_free();
void MAM_Zombies_update();

#endif
