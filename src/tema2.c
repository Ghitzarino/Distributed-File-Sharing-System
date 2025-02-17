#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define MAX_CLIENTS 100
#define MAX_MESSAGE 20

typedef struct {
  char filename[MAX_FILENAME];
  int chunk_count;
  char hashes[MAX_CHUNKS][HASH_SIZE + 1];
  int swarm_size;
  int client_type[MAX_CLIENTS];  // 0 - leech, 1 - seed
} SwarmFile;

typedef struct {
  char filename[MAX_FILENAME];
  int chunk_count;
  char hashes[MAX_CHUNKS][HASH_SIZE + 1];
} FileInfo;

typedef struct {
  int client_rank;
  int uploaded_files;
} ClientUploadInfo;

int file_count, wanted_count, uploaded_files_count;
FileInfo files[MAX_FILES];
char wanted_files[MAX_FILES][MAX_FILENAME];

void read_input_file(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    perror("Eroare la deschiderea fisierului de intrare");
    exit(-1);
  }

  // Citeste fisierele detinute
  fscanf(fp, "%d", &file_count);
  for (int i = 0; i < file_count; i++) {
    fscanf(fp, "%s %d", files[i].filename, &files[i].chunk_count);
    for (int j = 0; j < files[i].chunk_count; j++) {
      fscanf(fp, "%s", files[i].hashes[j]);
      files[i].hashes[j][HASH_SIZE] = '\0';
    }
  }

  // Citeste fisierele dorite
  fscanf(fp, "%d", &wanted_count);
  for (int i = 0; i < wanted_count; i++) {
    fscanf(fp, "%s", wanted_files[i]);
  }

  fclose(fp);
}

void save_file(int rank, const char *filename,
               const char hashes[MAX_CHUNKS][HASH_SIZE + 1], int chunk_count) {
  // Creeaza numele fisierului de iesire
  char output_filename[MAX_FILENAME + 20];
  snprintf(output_filename, sizeof(output_filename), "client%d_%s", rank,
           filename);

  FILE *output_file = fopen(output_filename, "w");
  if (output_file == NULL) {
    perror("Eroare la deschiderea fisierului de iesire");
    return;
  }

  // Scrie hash-urile in fisier
  for (int i = 0; i < chunk_count; i++) {
    fprintf(output_file, "%s\n", hashes[i]);
  }

  fclose(output_file);
}

void add_client_to_swarm(SwarmFile *file, int client_rank) {
  file->client_type[client_rank] = 1;
}

// Functie compare pentru sortare
int compare_uploaded_files(const void *a, const void *b) {
  ClientUploadInfo *clientA = (ClientUploadInfo *)a;
  ClientUploadInfo *clientB = (ClientUploadInfo *)b;
  return clientA->uploaded_files - clientB->uploaded_files;
}

void *download_thread_func(void *arg) {
  int rank = *(int *)arg;

  for (int i = 0; i < wanted_count; i++) {
    char wanted_file[MAX_FILENAME];
    strcpy(wanted_file, wanted_files[i]);

    // Mesaj catre tracker pentru a cere lista swarm-ului
    MPI_Send("REQ_SWARM", MAX_MESSAGE, MPI_CHAR, TRACKER_RANK, 2,
             MPI_COMM_WORLD);
    MPI_Send(wanted_file, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 2,
             MPI_COMM_WORLD);

    // Primeste swarm-ul
    SwarmFile swarm;
    MPI_Recv(&swarm, sizeof(SwarmFile), MPI_BYTE, TRACKER_RANK, 2,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Primeste lista cu nr de upload-uri ale clientilor si o sorteaza
    ClientUploadInfo clients[MAX_CLIENTS];
    MPI_Recv(clients, MAX_CLIENTS * sizeof(ClientUploadInfo), MPI_BYTE,
             TRACKER_RANK, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    qsort(clients, swarm.swarm_size - 1, sizeof(ClientUploadInfo),
          compare_uploaded_files);

    // Verifica daca fisierul dorit este gasit in swarm
    if (swarm.chunk_count == 0) {
      printf("Client [%d]: Fisierul %s nu a fost gasit in swarm.\n", rank,
             wanted_file);
      continue;
    }

    // Vector pentru a marca segmentele descarcate
    int chunks_downloaded[MAX_CHUNKS] = {0};
    int chunks_done = 0;

    while (chunks_done < swarm.chunk_count) {
      // Actualizeaza lista swarm-ului si upload-urilor dupa fiecare 10 segmente
      // descarcate si sorteaza lista de upload-uri crescator
      if (chunks_done % 10 == 0 && chunks_done > 0) {
        MPI_Send("REQ_SWARM", MAX_MESSAGE, MPI_CHAR, TRACKER_RANK, 2,
                 MPI_COMM_WORLD);
        MPI_Send(wanted_file, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 2,
                 MPI_COMM_WORLD);
        MPI_Recv(&swarm, sizeof(SwarmFile), MPI_BYTE, TRACKER_RANK, 2,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(clients, MAX_CLIENTS * sizeof(ClientUploadInfo), MPI_BYTE,
                 TRACKER_RANK, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        qsort(clients, swarm.swarm_size - 1, sizeof(ClientUploadInfo),
              compare_uploaded_files);
      }

      // Cauta un segment care nu este descarcat
      int chunk_to_request = -1;
      for (int j = 0; j < swarm.chunk_count; j++) {
        if (!chunks_downloaded[j]) {
          chunk_to_request = j;
          break;
        }
      }

      // Cauta un client seed pentru a cere un segment (sortat dupa cele mai
      // putine upload-uri)
      for (int j = 0; j < swarm.swarm_size - 1; j++) {
        int idx = clients[j].client_rank;
        if (swarm.client_type[idx] == 1 && idx != rank) {
          int seed_rank = idx;

          // Trimite cererea pentru segment catre seed
          MPI_Send("REQ_CHUNK", MAX_MESSAGE, MPI_CHAR, seed_rank, 0,
                   MPI_COMM_WORLD);
          MPI_Send(wanted_file, MAX_FILENAME, MPI_CHAR, seed_rank, 0,
                   MPI_COMM_WORLD);

          // Trimite hash-ul segmentului catre seed
          char chunk_hash[HASH_SIZE + 1];
          strcpy(chunk_hash, swarm.hashes[chunk_to_request]);
          MPI_Send(chunk_hash, HASH_SIZE + 1, MPI_CHAR, seed_rank, 0,
                   MPI_COMM_WORLD);

          // Primeste raspunsul de la seed
          char response[MAX_MESSAGE];
          MPI_Recv(response, MAX_MESSAGE, MPI_CHAR, seed_rank, 1,
                   MPI_COMM_WORLD, MPI_STATUS_IGNORE);

          if (strcmp(response, "OK") == 0) {
            // Marcheaza segmentul ca descarcat
            chunks_downloaded[chunk_to_request] = 1;
            chunks_done++;

            // Adauga chunk-ul descarcat in vectorul de fisiere detinute
            char chunk_hash[HASH_SIZE + 1];
            strcpy(chunk_hash, swarm.hashes[chunk_to_request]);

            // Verifica daca fisierul exista deja in vectorul files
            int file_index = -1;
            for (int i = 0; i < file_count; i++) {
              if (strcmp(files[i].filename, wanted_file) == 0) {
                file_index = i;
                break;
              }
            }

            // Daca fisierul nu exista, adauga unul nou
            if (file_index == -1) {
              file_index = file_count;
              strcpy(files[file_index].filename, wanted_file);
              files[file_index].chunk_count = swarm.chunk_count;
              memset(files[file_index].hashes, 0,
                     sizeof(files[file_index].hashes));
              file_count++;
            }

            // Insereaza chunk-ul la pozitia specificata
            strcpy(files[file_index].hashes[chunk_to_request], chunk_hash);

            // printf(
            //     "Client [%d]: Segmentul %d din fisierul %s a fost descarcat"
            //     "de la clientul [%d].\n",
            //     rank, chunk_to_request, wanted_file, seed_rank);
            break;
          } else if (strcmp(response, "ERR") == 0) {
            // Seed-ul nu detine segmentul, incearca alt client
            // printf(
            //     "Client [%d]: Clientul [%d] nu detine segmentul %d pentru "
            //     "fisierul %s.\n",
            //     rank, seed_rank, chunk_to_request, wanted_file);
          }
        }
      }
    }

    // printf("Client [%d]: Fisierul %s a fost descarcat complet.\n", rank,
    //        wanted_file);
    save_file(rank, wanted_file, swarm.hashes, swarm.chunk_count);
  }

  // Semnaleaza ca toate fisierele au fost descarcate pentru acest client
  MPI_Send("CLIENT_FIN", MAX_MESSAGE, MPI_CHAR, TRACKER_RANK, 2,
           MPI_COMM_WORLD);

  return NULL;
}

void *upload_thread_func(void *arg) {
  // int rank = *(int *)arg;

  while (1) {
    MPI_Status status;
    char message_type[MAX_MESSAGE];

    // Asteapta un mesaj de tip de la oricare alt client
    MPI_Recv(message_type, MAX_MESSAGE, MPI_CHAR, MPI_ANY_SOURCE, 0,
             MPI_COMM_WORLD, &status);
    int sender_rank = status.MPI_SOURCE;

    // Verifica daca primeste request de chunk
    if (strcmp(message_type, "REQ_CHUNK") == 0) {
      char requested_file[MAX_FILENAME];
      char requested_hash[HASH_SIZE + 1];

      // Primeste numele fisierului si hash-ul segmentului cerut
      MPI_Recv(requested_file, MAX_FILENAME, MPI_CHAR, sender_rank, 0,
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Recv(requested_hash, HASH_SIZE + 1, MPI_CHAR, sender_rank, 0,
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);

      // Cauta fisierul si verifica daca are segmentul cerut
      int chunk_found = 0;
      for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].filename, requested_file) == 0) {
          for (int j = 0; j < files[i].chunk_count; j++) {
            if (strcmp(files[i].hashes[j], requested_hash) == 0) {
              chunk_found = 1;
              break;
            }
          }
        }
      }

      // Trimite raspunsul "OK" daca segmentul este gasit, altfel "ERR"
      if (chunk_found) {
        uploaded_files_count++;
        MPI_Send("OK", MAX_MESSAGE, MPI_CHAR, sender_rank, 1, MPI_COMM_WORLD);
      } else {
        MPI_Send("ERR", MAX_MESSAGE, MPI_CHAR, sender_rank, 1, MPI_COMM_WORLD);
      }
    } else if (strcmp(message_type, "REQ_UPLD_INFO") == 0) {
      // Trimite nr de upload-uri catre tracker
      MPI_Send(&uploaded_files_count, 1, MPI_INT, TRACKER_RANK, 0,
               MPI_COMM_WORLD);
    } else if (strcmp(message_type, "CLOSE") == 0) {
      // Inchide threadul daca primeste mesajul final de la tracker
      break;
    }
  }

  return NULL;
}

void peer(int numtasks, int rank) {
  pthread_t download_thread;
  pthread_t upload_thread;
  void *status;
  int r;

  // Citeste fisierul de intrare
  char input_filename[MAX_FILENAME];
  snprintf(input_filename, sizeof(input_filename), "in%d.txt", rank);
  read_input_file(input_filename);

  // Trimite informatiile catre tracker
  MPI_Send(&file_count, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
  for (int i = 0; i < file_count; i++) {
    MPI_Send(&files[i], sizeof(FileInfo), MPI_BYTE, TRACKER_RANK, 0,
             MPI_COMM_WORLD);
  }

  // Asteapta "ACK" de la tracker
  char ack_message[MAX_MESSAGE];
  MPI_Recv(ack_message, MAX_MESSAGE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD,
           MPI_STATUS_IGNORE);

  if (strcmp(ack_message, "ACK") == 0) {
    // printf(
    //     "Client [%d]: Am primit ACK de la tracker, pot incepe"
    //     "descarcarea.\n",
    //     rank);
  }

  // Pornirea thread-urilor pentru upload si download
  r = pthread_create(&download_thread, NULL, download_thread_func,
                     (void *)&rank);
  if (r) {
    printf("Eroare la crearea thread-ului de download\n");
    exit(-1);
  }

  r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&rank);
  if (r) {
    printf("Eroare la crearea thread-ului de upload\n");
    exit(-1);
  }

  r = pthread_join(download_thread, &status);
  if (r) {
    printf("Eroare la așteptarea thread-ului de download\n");
    exit(-1);
  }

  r = pthread_join(upload_thread, &status);
  if (r) {
    printf("Eroare la așteptarea thread-ului de upload\n");
    exit(-1);
  }

  // printf("Client [%d]: Am incarcat %d de segmente.\n", rank,
  //        uploaded_files_count);
}

void tracker(int numtasks, int rank) {
  SwarmFile all_files[MAX_FILES];
  int total_files = 0;

  // Initilizeaza vectorul de uploaduri pentru fiecare client
  ClientUploadInfo clients[MAX_CLIENTS];
  memset(clients, 0, sizeof(clients));

  for (int i = 1; i < numtasks; i++) {
    clients[i - 1].client_rank = i;
  }

  // Primeste informatiile de la toti clientii
  for (int i = 1; i < numtasks; i++) {
    int client_file_count;
    MPI_Recv(&client_file_count, 1, MPI_INT, i, 0, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);

    for (int j = 0; j < client_file_count; j++) {
      SwarmFile new_file = {0};
      MPI_Recv(&new_file, sizeof(FileInfo), MPI_BYTE, i, 0, MPI_COMM_WORLD,
               MPI_STATUS_IGNORE);

      // Verifica daca fisierul exista deja in vector
      int file_exists = 0;
      for (int k = 0; k < total_files; k++) {
        if (strcmp(all_files[k].filename, new_file.filename) == 0) {
          add_client_to_swarm(&all_files[k], i);
          file_exists = 1;
          break;
        }
      }

      // Adauga fisierul si initializeaza swarm-ul daca fisierul nu exista
      if (!file_exists) {
        new_file.swarm_size = numtasks;
        add_client_to_swarm(&new_file, i);

        all_files[total_files++] = new_file;
      }
    }
  }

  // Raspunde fiecarui client cu un "ACK"
  for (int i = 1; i < numtasks; i++) {
    MPI_Send("ACK", MAX_MESSAGE, MPI_CHAR, i, 0, MPI_COMM_WORLD);
  }

  int finished_clients = 0;

  // Asculta pentru requesturi de la clienti
  while (1) {
    MPI_Status status;
    char message_type[MAX_MESSAGE];
    MPI_Recv(message_type, MAX_MESSAGE, MPI_CHAR, MPI_ANY_SOURCE, 2,
             MPI_COMM_WORLD, &status);

    int sender_rank = status.MPI_SOURCE;

    if (strcmp(message_type, "REQ_SWARM") == 0) {
      // Primeste numele fisierului
      char requested_filename[MAX_FILENAME];
      MPI_Recv(requested_filename, MAX_FILENAME, MPI_CHAR, sender_rank, 2,
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);

      // Gaseste fisierul cerut
      int file_found = 0;
      for (int i = 0; i < total_files; i++) {
        if (strcmp(all_files[i].filename, requested_filename) == 0) {
          file_found = 1;

          // Trimite informatia despre swarm la client
          MPI_Send(&all_files[i], sizeof(SwarmFile), MPI_BYTE, sender_rank, 2,
                   MPI_COMM_WORLD);

          // Marcheaza clientul ca seed
          add_client_to_swarm(&all_files[i], sender_rank);
          break;
        }
      }

      if (!file_found) {
        printf("Asta nu ar trebui sa se intample!\n");
      }

      // Updateaza intreaga lista de upload-uri
      for (int i = 1; i < numtasks; i++) {
        MPI_Send("REQ_UPLD_INFO", MAX_MESSAGE, MPI_CHAR, i, 0, MPI_COMM_WORLD);

        int uploaded_files_cnt;
        MPI_Recv(&uploaded_files_cnt, 1, MPI_INT, i, 0, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);

        clients[i - 1].uploaded_files = uploaded_files_cnt;
      }

      // Trimite vectorul de upload-uri pentru actualizare
      MPI_Send(clients, MAX_CLIENTS * sizeof(ClientUploadInfo), MPI_BYTE,
               sender_rank, 3, MPI_COMM_WORLD);

    } else if (strcmp(message_type, "CLIENT_FIN") == 0) {
      // Un client a terminat de downloadat
      finished_clients++;

      // Daca toti clientii au terminat atunci semnaleaza la upload thread sa
      // inchida si termina rularea programului
      if (finished_clients == numtasks - 1) {
        for (int i = 1; i < numtasks; i++) {
          MPI_Send("CLOSE", MAX_MESSAGE, MPI_CHAR, i, 0, MPI_COMM_WORLD);
        }
        break;
      }
    }
  }
}

int main(int argc, char *argv[]) {
  int numtasks, rank;

  int provided;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  if (provided < MPI_THREAD_MULTIPLE) {
    fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
    exit(-1);
  }
  MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == TRACKER_RANK) {
    tracker(numtasks, rank);
  } else {
    peer(numtasks, rank);
  }

  MPI_Finalize();
}
