#include <emu/tls.h>
#include <pthread.h>

static pthread_key_t tls_key;
static pthread_key_t tls_name_key;
static pthread_once_t tls_once = PTHREAD_ONCE_INIT;

static void tls_init_keys(void)
{
  pthread_key_create(&tls_key, NULL);
  pthread_key_create(&tls_name_key, NULL);
}

void TLS_set(void *value)
{
  pthread_once(&tls_once, tls_init_keys);
  pthread_setspecific(tls_key, value);
}

void *TLS_get(void)
{
  pthread_once(&tls_once, tls_init_keys);
  return pthread_getspecific(tls_key);
}

void TLS_setName(const char *value)
{
  pthread_once(&tls_once, tls_init_keys);
  pthread_setspecific(tls_name_key, (void *)value);
}

const char *TLS_getName()
{
  pthread_once(&tls_once, tls_init_keys);
  return (const char *)pthread_getspecific(tls_name_key);
}
