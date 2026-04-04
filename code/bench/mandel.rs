fn mandel_checksum(width: i32, height: i32, max_iter: i32) -> i64 {
    let mut total: i64 = 0;
    let inv_w = 1.0f64 / width as f64;
    let inv_h = 1.0f64 / height as f64;
    for y in 0..height {
        let ci = (y as f64 * inv_h) * 2.0 - 1.0;
        for x in 0..width {
            let cr = (x as f64 * inv_w) * 3.5 - 2.5;
            let mut zr = 0.0_f64;
            let mut zi = 0.0_f64;
            let mut escaped = false;
            let mut it: i32 = 0;
            for i in 0..max_iter {
                if !escaped {
                    let zr2 = zr * zr - zi * zi + cr;
                    let zi2 = 2.0 * zr * zi + ci;
                    zr = zr2;
                    zi = zi2;
                    if zr * zr + zi * zi > 4.0 {
                        escaped = true;
                        it = i;
                    }
                }
            }
            if !escaped {
                it = max_iter;
            }
            total += it as i64;
        }
    }
    total
}

fn main() {
    let width = 1000;
    let height = 1000;
    let max_iter = 500;
    let result = mandel_checksum(width, height, max_iter);
    println!("{}", result);
}
