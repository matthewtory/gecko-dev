// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::cmp::{max, min};
use std::time::{Duration, Instant};

pub const LOCAL_IDLE_TIMEOUT: Duration = Duration::from_secs(30);

#[derive(Debug, Clone)]
/// There's a little bit of different behavior for resetting idle timeout. See
/// -transport 10.2 ("Idle Timeout").
enum IdleTimeoutState {
    Init,
    New(Instant),
    PacketReceived(Instant),
    AckElicitingPacketSent(Instant),
}

#[derive(Debug, Clone)]
/// There's a little bit of different behavior for resetting idle timeout. See
/// -transport 10.2 ("Idle Timeout").
pub struct IdleTimeout {
    timeout: Duration,
    state: IdleTimeoutState,
}

#[cfg(test)]
impl IdleTimeout {
    pub fn new(timeout: Duration) -> Self {
        Self {
            timeout,
            state: IdleTimeoutState::Init,
        }
    }
}

impl Default for IdleTimeout {
    fn default() -> Self {
        Self {
            timeout: LOCAL_IDLE_TIMEOUT,
            state: IdleTimeoutState::Init,
        }
    }
}

impl IdleTimeout {
    pub fn set_peer_timeout(&mut self, peer_timeout: Duration) {
        self.timeout = min(self.timeout, peer_timeout);
    }

    pub fn expiry(&mut self, now: Instant, pto: Duration) -> Instant {
        let start = match self.state {
            IdleTimeoutState::Init => {
                self.state = IdleTimeoutState::New(now);
                now
            }
            IdleTimeoutState::New(t)
            | IdleTimeoutState::PacketReceived(t)
            | IdleTimeoutState::AckElicitingPacketSent(t) => t,
        };
        start + max(self.timeout, pto * 3)
    }

    pub fn on_packet_sent(&mut self, now: Instant) {
        // Only reset idle timeout if we've received a packet since the last
        // time we reset the timeout here.
        match self.state {
            IdleTimeoutState::AckElicitingPacketSent(_) => {}
            IdleTimeoutState::Init
            | IdleTimeoutState::New(_)
            | IdleTimeoutState::PacketReceived(_) => {
                self.state = IdleTimeoutState::AckElicitingPacketSent(now);
            }
        }
    }

    pub fn on_packet_received(&mut self, now: Instant) {
        self.state = IdleTimeoutState::PacketReceived(now);
    }

    pub fn expired(&mut self, now: Instant, pto: Duration) -> bool {
        now >= self.expiry(now, pto)
    }
}
