/* Reuse the existing real-TCP fixture, not a mock transport or duplicated FTP
   parser. Only this test's engine object renames setsockopt/getsockopt. */
#define main existing_ftp_suite_main
#include "assistant_ftp_tests.c"
#undef main

static int option_failure;
static unsigned int receive_sets, send_sets;

int assistant_test_setsockopt(int fd, int level, int option, const void *value, socklen_t size)
{
    if (level == SOL_SOCKET && (option == SO_RCVBUF || option == SO_SNDBUF)) {
        assert(size == sizeof(int) && *(const int *)value == 32768);
        if (option == SO_RCVBUF) receive_sets++;
        else send_sets++;
        if (option_failure == 1 && option == SO_RCVBUF) { errno = ENOPROTOOPT; return -1; }
    }
    return setsockopt(fd, level, option, value, size);
}

int assistant_test_getsockopt(int fd, int level, int option, void *value, socklen_t *size)
{
    if (option_failure == 2 && level == SOL_SOCKET && option == SO_RCVBUF) {
        errno = EIO;
        return -1;
    }
    return getsockopt(fd, level, option, value, size);
}

static AssistantFtpDiagnostics diagnostics(Fixture *f)
{
    AssistantFtpDiagnostics value;
    assert(assistant_ftp_get_diagnostics(f->server, &value));
    return value;
}

static void option_result_does_not_change_acceptance(int failure)
{
    option_failure = failure;
    receive_sets = send_sets = 0;
    Fixture *f = create(); authenticate(f);
    int data = start_file(f, "folder/payload.bin");
    AssistantFtpDiagnostics d = diagnostics(f);
    assert(receive_sets == 1 && send_sets == 1);
    assert(d.receive_buffer.supported && d.receive_buffer.attempted && d.receive_buffer.requested == 32768);
    assert(d.send_buffer.supported && d.send_buffer.attempted && d.send_buffer.requested == 32768);
    assert(d.send_buffer.set_error == 0 && d.send_buffer.after_error == 0 && d.send_buffer.after_value > 0);
    assert(d.receive_buffer.set_error == (failure == 1 ? ENOPROTOOPT : 0));
    if (failure == 2) {
        assert(d.receive_buffer.before_error == EIO && d.receive_buffer.after_error == EIO);
        assert(d.receive_buffer.before_value == 0 && d.receive_buffer.after_value == 0);
    } else {
        assert(d.receive_buffer.before_error == 0 && d.receive_buffer.after_error == 0);
        /* Linux may double the requested buffer size. Observe, never assume. */
        assert(d.receive_buffer.after_value > 0);
    }
    assert(!d.window_scale.supported && !d.window_scale.attempted && d.window_scale.set_error == ENOTSUP);

    /* Partial data is still neither complete nor publishable, with or without
       an accepted tuning option. Metrics must not manufacture EOF. */
    send_bytes(data, "pay", 3); wait_size(f, 3, 1);
    d = diagnostics(f);
    assert(d.received_bytes == 3 && d.positive_receives == 3 && d.end_of_file == 0);
    assert(f->sink.completes == 0 && assistant_ftp_busy(f->server));
    for (int i = 0; i < 4; i++) pump(f, 32);
    assert(diagnostics(f).would_block > 0);
    send_bytes(data, "load", 4); wait_size(f, 7, 2);
    assert(f->sink.completes == 0);
    clear_received(f); shutdown(data, SHUT_WR); wait_text(f, "226 "); close(data);
    d = diagnostics(f);
    assert(d.received_bytes == 7 && d.end_of_file == 1 && d.receive_errors == 0);
    assert(d.positive_receives >= 5 && d.positive_receives <= 7);
    assert(d.receive_size_buckets[0] == d.positive_receives);
    assert(d.receive_calls == d.positive_receives + d.would_block + d.interrupted + d.end_of_file);
    assert(!memcmp(f->sink.bytes, "payload", 7) && f->sink.completes == 1);
    assert(assistant_ftp_seal(f->server));
    dispose(f);
}

static void metrics_restart_and_do_not_hide_overrun(void)
{
    option_failure = 0;
    Fixture *f = create(); authenticate(f);
    int data = start_file(f, "folder/payload.bin");
    send_bytes(data, "payload", 7); clear_received(f); shutdown(data, SHUT_WR); wait_text(f, "226 "); close(data);
    assert(diagnostics(f).received_bytes == 7);
    f->sink.expected = 0;
    data = start_file(f, "zero.bin");
    assert(diagnostics(f).received_bytes == 0 && diagnostics(f).end_of_file == 0);
    clear_received(f); shutdown(data, SHUT_WR); wait_text(f, "226 "); close(data);
    assert(diagnostics(f).received_bytes == 0 && diagnostics(f).end_of_file == 1);
    assert(f->sink.completes == 2 && assistant_ftp_seal(f->server)); dispose(f);

    f = create(); authenticate(f); data = start_file(f, "folder/payload.bin");
    send_bytes(data, "overflow", 8); clear_received(f); shutdown(data, SHUT_WR); wait_text(f, "426 "); close(data);
    assert(diagnostics(f).received_bytes == 8); /* Observed wire bytes, not accepted bytes. */
    assert(f->sink.used <= 7 && f->sink.completes == 0 && assistant_ftp_failed(f->server));
    assert(!assistant_ftp_seal(f->server)); dispose(f);
    assert(!assistant_ftp_get_diagnostics(NULL, NULL));
}

int main(void)
{
    option_result_does_not_change_acceptance(0);
    option_result_does_not_change_acceptance(1);
    option_result_does_not_change_acceptance(2);
    metrics_restart_and_do_not_hide_overrun();
    puts("Experimental socket diagnostics passed: real TCP, option success/failure/readback unknown, partial EOF, reset, overrun and unchanged acceptance.");
    return 0;
}
