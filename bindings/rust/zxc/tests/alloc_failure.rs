// SPDX-License-Identifier: BSD-3-Clause
//! A forged footer must not abort the process. Its own binary: the global
//! allocator here refuses what a small machine would.

use std::alloc::{GlobalAlloc, Layout, System};

struct Capped;

const CAP: usize = 1 << 30;

unsafe impl GlobalAlloc for Capped {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if layout.size() > CAP {
            return std::ptr::null_mut();
        }
        unsafe { System.alloc(layout) }
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) }
    }
}

#[global_allocator]
static ALLOCATOR: Capped = Capped;

fn minimal_bytes(x: u64) -> usize {
    (64 - x.leading_zeros() as usize).div_ceil(8).max(1)
}

/// Rewrites the footer of a checksum-free frame to claim `claim` bytes.
fn with_claimed_size(frame: &[u8], claim: u64) -> Vec<u8> {
    let lens = frame[frame.len() - 1] as usize;
    let body = &frame[..frame.len() - ((lens & 7) + 1) - ((lens >> 4) + 1) - 1];
    let nd = minimal_bytes(claim);
    let nf = (1..=8)
        .find(|&nf| minimal_bytes((body.len() + nd + nf + 1) as u64) == nf)
        .unwrap();
    let total = (body.len() + nd + nf + 1) as u64;
    let mut forged = body.to_vec();
    forged.extend_from_slice(&claim.to_le_bytes()[..nd]);
    forged.extend_from_slice(&total.to_le_bytes()[..nf]);
    forged.push(((nd - 1) | ((nf - 1) << 4)) as u8);
    forged
}

#[test]
fn forged_footer_is_an_error_not_an_abort() {
    let mut rng: u32 = 7;
    let payload: Vec<u8> = (0..1 << 20)
        .map(|_| {
            rng = rng.wrapping_mul(1_103_515_245).wrapping_add(12_345);
            (rng >> 16) as u8
        })
        .collect();
    let frame = zxc::compress(&payload, zxc::Level::Fastest, None).unwrap();

    // Each block costs at least 8 bytes and decodes to at most the block size.
    let claim = (frame.len() as u64 / 8) * zxc::frame_info(&frame).unwrap().block_size as u64;
    assert!(claim > CAP as u64);
    let forged = with_claimed_size(&frame, claim);
    assert_eq!(zxc::decompressed_size(&forged), Some(claim));

    assert!(matches!(zxc::decompress(&forged), Err(zxc::Error::Memory)));
}
