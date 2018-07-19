fn add_one(x: i32) -> i32 {
    x + 1
}

fn do_twice(f: fn(i32) -> i32, arg: i32) -> i32 {
    f(arg) + f(arg)
}

fn returns_closure() -> Box<Fn(i32) -> i32> {
    Box::new(|x| x + 1)
}

fn main() {
    let answer = do_twice(add_one, 5);

    println!("The answer is {}", answer);

    let _list_of_strings: Vec<String> = vec![1, 2, 3]
        .iter()
        .map(|i| i.to_string())
        .collect();

    let _another_list_of_strings: Vec<String> = vec![1, 2, 3]
        .iter()
        .map(ToString::to_string)
        .collect();
}