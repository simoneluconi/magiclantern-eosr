#include "ptp.h"
#include "dryos.h"

int recv_ptp_data(struct ptp_context *data, char *buf, int size)
	// repeated calls per transaction are ok
{
	while ( size >= BUF_SIZE )
	{
		data->recv_data(data->handle,buf,BUF_SIZE,0,0);
		// XXX check for success??

		size -= BUF_SIZE;
		buf += BUF_SIZE;
	}
	if ( size != 0 )
	{
		data->recv_data(data->handle,buf,size,0,0);
		// XXX check for success??
	}

	return 1;
}

#ifdef CONFIG_PTP_ASYNC_SEND
/* On these cameras send_data() is asynchronous: it takes a completion
 * callback and its argument, and calls the callback when the transfer is
 * done. Canon's own handlers (e.g. GetDeviceInfo, GetObject on R6 1.9.0)
 * pass a { semaphore, status } pair, the callback stores the status and
 * gives the semaphore, and the handler waits on it. A NULL callback
 * crashes the camera (ERR70) when the transfer completes. */
struct ptp_send_wait
{
	struct semaphore * sem;
	int status;
};

static int ptp_send_done(struct ptp_send_wait *w, int status)
{
	w->status = status;
	give_semaphore(w->sem);
	return 0;
}

/* Static: if a wait times out, a late callback must not write into a
 * dead stack frame. PTP handlers run one at a time. */
static struct ptp_send_wait ptp_send_wait;

int ptp_send_part(struct ptp_context *data, const void *buf, int size, int total)
{
	if (!ptp_send_wait.sem)
		ptp_send_wait.sem = create_named_semaphore("ptp_send", 0);

	ptp_send_wait.status = 0;
	int ret = data->send_data(data->handle, (void *)buf, size, total,
	                          (int)&ptp_send_done, (int)&ptp_send_wait, 0);
	if (ret & 1)
		return ret;
	if (take_semaphore(ptp_send_wait.sem, 10000) & 1)
		return 1;   // timed out
	return ptp_send_wait.status & 1;
}
#else
int ptp_send_part(struct ptp_context *data, const void *buf, int size, int total)
{
	return data->send_data(data->handle, (void *)buf, size, total, 0, 0, 0);
}
#endif

int send_ptp_data(struct ptp_context *data, const char *buf, int size)
	// repeated calls per transaction are *not* ok
{
	int tmpsize;

	tmpsize = size;
	while ( size >= BUF_SIZE )
	{
		if ( ptp_send_part(data,buf,BUF_SIZE,tmpsize) )
		{
			return 0;
		}

		tmpsize = 0;
		size -= BUF_SIZE;
		buf += BUF_SIZE;
	}
	if ( size != 0 )
	{
		if ( ptp_send_part(data,buf,size,tmpsize) )
		{
			return 0;
		}
	}

	return 1;
}

uint32_t ptp_register_all_handlers()
{
	uint32_t ret = 0x0;
	
	extern struct ptp_handler _ptp_handlers_start[];
	extern struct ptp_handler _ptp_handlers_end[];

	struct ptp_handler * handler = _ptp_handlers_start;

	for( ; handler < _ptp_handlers_end ; handler++ )
	{
#if defined(POSITION_INDEPENDENT)
        handler->handler = PIC_RESOLVE(handler->handler);
        handler->priv = PIC_RESOLVE(handler->priv);
#endif
		//DebugMsg("[ML] PTP_INIT reg: id=0x%08X h=0x%08X p=0x%08X", handler->id, handler->handler, handler->priv);		
		ret |= ptp_register_handler(
				handler->id,
				handler->handler,
				handler->priv
				);
	}

	return ret;
}

static void ptp_init( void *unused )
{
#ifndef CONFIG_40D 
	ptp_register_all_handlers();
#endif
}

INIT_FUNC( __FILE__, ptp_init );
