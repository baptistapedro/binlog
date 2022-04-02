#include <binlog/SessionWriter.hpp>

#include <binlog/Session.hpp>
#include <binlog/create_source.hpp>

#include "test_utils.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("add_event")
{
  binlog::Session session;
  binlog::SessionWriter writer(session, 128);

  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={} b={}", "i[c");
  CHECK(writer.addEvent(eventSourceid, 0, 456, std::string("foo")));

  CHECK(getEvents(session, "%m") == std::vector<std::string>{"a=456 b=foo"});
}

TEST_CASE("add_event_with_time")
{
  binlog::Session session;
  binlog::SessionWriter writer(session, 128);

  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={} b={}", "i[c");

  const auto now = std::chrono::system_clock::now();
  const auto clock = std::uint64_t(now.time_since_epoch().count());
  CHECK(writer.addEvent(eventSourceid, clock, 456, std::string("foo")));

  CHECK(getEvents(session, "%d %m") == std::vector<std::string>{timePointToString(now) + " a=456 b=foo"});
}

TEST_CASE("set_clock_sync_and_add_event_with_time")
{
  binlog::Session session;
  binlog::SessionWriter writer(session, 128);

  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={} b={}", "i[c");

  const binlog::ClockSync clockSync{0, 1, 100 * std::nano::den, 0, "UTC"};
  session.setClockSync(clockSync);

  CHECK(writer.addEvent(eventSourceid, 123, 456, std::string("foo")));

  CHECK(getEvents(session, "%d %m") == std::vector<std::string>{"1970.01.01 00:03:43 a=456 b=foo"});
}

TEST_CASE("reset_clock_sync_and_add_events_with_time")
{
  binlog::Session session;
  binlog::SessionWriter writer(session, 128);
  TestStream stream;
  session.consume(stream);

  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={} b={}", "i[c");

  const binlog::ClockSync clockSync{0, 1, 100 * std::nano::den, 0, "UTC"};
  session.setClockSync(clockSync);
  CHECK(writer.addEvent(eventSourceid, 123, 456, std::string("foo")));
  session.consume(stream);

  const binlog::ClockSync clockSync2{0, 2, 200 * std::nano::den, 0, "UTC"};
  session.setClockSync(clockSync2);
  CHECK(writer.addEvent(eventSourceid, 122, 789, std::string("bar")));
  session.consume(stream);

  const std::vector<std::string> expectedEvents{
    "1970.01.01 00:03:43 a=456 b=foo",
    "1970.01.01 00:04:21 a=789 b=bar",
  };
  CHECK(streamToEvents(stream, "%d %m") == expectedEvents);
}

TEST_CASE("add_event_with_writer_id_name")
{
  binlog::Session session;
  binlog::SessionWriter writer(session, 128);

  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={} b={}", "i[c");

  writer.setId(111);
  writer.setName("John");
  CHECK(writer.addEvent(eventSourceid, 0, 456, std::string("foo")));

  CHECK(getEvents(session, "%t %n %m") == std::vector<std::string>{"111 John a=456 b=foo"});
}

TEST_CASE("add_event_with_writer_id_name_ctor")
{
  binlog::Session session;
  binlog::SessionWriter writer(session, 128, 111, "John");

  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={} b={}", "i[c");
  CHECK(writer.addEvent(eventSourceid, 0, 456, std::string("foo")));

  CHECK(getEvents(session, "%t %n %m") == std::vector<std::string>{"111 John a=456 b=foo"});
}

TEST_CASE("add_event_then_close")
{
  // Make sure event reaches the consumer, even after the producer is destructed

  binlog::Session session;

  {
    binlog::SessionWriter writer(session, 128);

    BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={} b={}", "i[c");
    CHECK(writer.addEvent(eventSourceid, 0, 456, std::string("foo")));
  }

  CHECK(getEvents(session, "%m") == std::vector<std::string>{"a=456 b=foo"});
}

TEST_CASE("consume_metadata_twice")
{
  binlog::Session session;
  binlog::SessionWriter writer(session, 128);

  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={} b={}", "i[c");

  CHECK(writer.addEvent(eventSourceid, 0, 123, std::string("foo")));
  getEvents(session, ""); // consume metadata and data

  const auto now = std::chrono::system_clock::now();
  const auto clock = std::uint64_t(now.time_since_epoch().count());
  CHECK(writer.addEvent(eventSourceid, clock, 456, std::string("bar")));

  TestStream stream;
  session.reconsumeMetadata(stream); // add clock sync and event source
  session.consume(stream); // consume the second event

  CHECK(streamToEvents(stream, "%d %m") == std::vector<std::string>{timePointToString(now) + " a=456 b=bar"});
}

TEST_CASE("add_events_from_threads")
{
  binlog::Session session;

  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={}", "i");

  auto writeEvents = [eventSourceid, &session](const char* name)
  {
    binlog::SessionWriter writer(session, 4096);
    writer.setName(name);

    for (int i = 0; i < 1000; ++i)
    {
      while (! writer.addEvent(eventSourceid, 0, i)) { std::this_thread::yield(); }
    }
  };

  std::thread threadA(writeEvents, "A");
  std::thread threadB(writeEvents, "B");

  TestStream out;
  std::atomic<bool> write_done{false};
  std::thread consumer([&session, &out, &write_done]()
  {
    while (! write_done.load())
    {
      session.consume(out);
    }

    // consume events written after last consume but before the loop condition was tested
    session.consume(out);
  });

  threadA.join();
  threadB.join();
  write_done.store(true);

  consumer.join();

  std::vector<std::string> events = streamToEvents(out, "%n %m");

  // order of events is not specified across threads: sort them by thread name
  std::stable_sort(
    events.begin(), events.end(),
    [](const std::string& a, const std::string& b) { return a[0] < b[0]; }
  );

  // generate expected events: not easy on memory, but keeps the report small
  std::vector<std::string> expectedEvents;
  expectedEvents.reserve(2000);
  for (char writerName : std::string{"AB"})
  {
    for (int i = 0; i < 1000; ++i)
    {
      std::ostringstream s;
      s << writerName << " a=" << i;
      expectedEvents.push_back(s.str());
    }
  }

  CHECK(events == expectedEvents);
}

TEST_CASE("queue_is_full")
{
  binlog::Session session;
  binlog::SessionWriter writer(session, 128);

  writer.setId(7);
  writer.setName("Seven");

  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={}", "[i");

  // add more data that would otherwise fit in the queue
  for (int i = 0; i < 512; ++i)
  {
    // even if the queue is full, writer allocates a new queue, returns true
    CHECK(writer.addEvent(eventSourceid, 0, std::vector<int>{i,i+1,i+2}));
  }

  std::vector<std::string> expectedEvents;
  expectedEvents.reserve(512);
  for (int i = 0; i < 512; ++i)
  {
    std::ostringstream s;
    s << "7 Seven a=[" << i << ", " << i+1 << ", " << i+2 << "]";
    expectedEvents.push_back(s.str());
  }

  TestStream stream;
  const binlog::Session::ConsumeResult cr = session.consume(stream);

  // make sure old channels are closed
  CHECK(cr.channelsPolled > 1);
  CHECK(cr.channelsRemoved + 1 == cr.channelsPolled);

  // make sure the events are correct, and the writer properties are preserved
  CHECK(streamToEvents(stream, "%t %n %m") == expectedEvents);
}

TEST_CASE("move_ctor")
{
  binlog::Session session;
  binlog::SessionWriter writerToBeMoved(session, 128);
  binlog::SessionWriter writer(std::move(writerToBeMoved));

  // channel is still operational
  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={} b={}", "i[c");
  CHECK(writer.addEvent(eventSourceid, 0, 456, std::string("foo")));

  // no channel is closed
  TestStream stream;
  const binlog::Session::ConsumeResult cr = session.consume(stream);
  CHECK(cr.channelsPolled == 1);
  CHECK(cr.channelsRemoved == 0);

  CHECK(streamToEvents(stream, "%m") == std::vector<std::string>{"a=456 b=foo"});
}

TEST_CASE("move_assign")
{
  binlog::Session session;
  binlog::SessionWriter writer1(session, 128);
  binlog::SessionWriter writer2(session, 128);

  writer1.setName("W1");
  writer2.setName("W2");

  BINLOG_CREATE_SOURCE(eventSourceid, "INFO", "cat", "a={} b={}", "i[c");

  CHECK(writer1.addEvent(eventSourceid, 0, 123, std::string("foo")));
  CHECK(writer2.addEvent(eventSourceid, 0, 456, std::string("bar")));

  writer2 = std::move(writer1);
  CHECK(writer2.addEvent(eventSourceid, 0, 789, std::string("baz")));

  const std::vector<std::string> expectedEvents{
    "W1 a=123 b=foo",
    "W1 a=789 b=baz",
    "W2 a=456 b=bar",
  };
  CHECK(getEvents(session, "%n %m") == expectedEvents);
}

TEST_CASE("swap_writers_of_different_sessions")
{
  binlog::Session sa;
  binlog::SessionWriter wa(sa, 128);

  binlog::Session sb;
  binlog::SessionWriter wb(sb, 128);

  std::swap(wa, wb);
  // at this point, wa references a channel of sb,
  // but sb is destructed first. ASAN will detect
  // if wa accesses a destructed channel.
}
